#include "log_reader.h"

#define MaxLogRecordSize	0x8000
#define MaxCallChainSize	0x8000

/* 从描述符获取大小 */
unsigned int LogReader::_get_log_block_size(LogHead& Head)
{
	unsigned char call_chain_length = 0;
	unsigned int log_block_size = sizeof(LogHead);

	if (Head.descriptor & LH_Mask_ThreadId)
		log_block_size += sizeof(DWORD);

	call_chain_length = Head.descriptor & LH_Mask_CallChainLength;
	if (call_chain_length)
		log_block_size += sizeof(DWORD) + call_chain_length * (this->is_wow64 ? sizeof(DWORD) : sizeof(PVOID));
	else if (Head.descriptor & LH_Mask_Omitted)
		log_block_size += sizeof(DWORD);

	return log_block_size;
}

LogReader::LogReader(PVOID pLogPage, PVOID pFlagPage, LoggerBrain& Brain, bool IsWow64)
{
	/* 读取操作占用 */
	this->shadow_head = 0;
	this->log_page = (PCHAR)pLogPage;
	this->flag_page = (PBYTE)pFlagPage;
	this->logger = &Brain;
	this->is_wow64 = IsWow64;

	/* 线程 */
	this->dead_request = false;
	this->pause_request = false;
	this->flush_request = false;
	this->skip_logging_request = false;
	this->h_reader =
		CreateThread(
			NULL,
			0,
			(LPTHREAD_START_ROUTINE)_reader_thread_func,
			this,
			CREATE_SUSPENDED,
			NULL
		);

	/* 同步操作 */
	InitializeCriticalSection(&this->log_lock);
	InitializeCriticalSection(&this->call_chain_lock);
	InitializeCriticalSection(&this->thread_index_lock);
}

LogReader::~LogReader()
{
	this->dead_request = true;
	WaitForSingleObject(h_reader, INFINITE);

	DeleteCriticalSection(&this->log_lock);
	DeleteCriticalSection(&this->call_chain_lock);
	DeleteCriticalSection(&this->thread_index_lock);
}

PCHAR LogReader::_read()
{
	DWORD cur_head = logger->head;
	DWORD cur_tail = logger->tail;

	/* 为空 */
	if (cur_tail == cur_head) {
		this->shadow_head = cur_head;
		return NULL;
	}

	/*
		不需要换行
		1. 头在尾前
		2. 尾在头前，头到边界处仍有效
	*/
	LogHead* p_temp_head;
	if (cur_tail > cur_head ||
		logger->capability - cur_head >= (int)sizeof(LogHead)) {
		p_temp_head = (LogHead*)((PCHAR)log_page + cur_head);
		if ((p_temp_head->descriptor & LH_Mask_Valid) == false)
			goto need_restart;
		shadow_head = cur_head + _get_log_block_size(*p_temp_head);
		// printf("<read> %d -> %d\n", cur_head, shadow_head);
		return cur_head + log_page;
	}

	/*
		需要换行
		1. 尾在头前，头到边界处无效
	*/
need_restart:
	p_temp_head = (LogHead*)log_page;
	shadow_head = _get_log_block_size(*p_temp_head);
	// printf("<read> 0 -> %d\n", shadow_head);
	return log_page;
}

void LogReader::_read_done()
{
	logger->head = shadow_head;
}

/* 只有在存在哈希值的情况下才被调用 */
void LogReader::_record_hash(LogHead& Head, DWORD Hash, PVOID lpCallChain)
{
	auto ite_hash = this->hash_to_call_chain.find(Hash);
	if (ite_hash != this->hash_to_call_chain.end()) {
		ite_hash->second.ref++;
		return;
	}

	int i;
	int call_chain_length = Head.descriptor & LH_Mask_CallChainLength;
	PVOID chain_value;
	Priv_CallChainInfo chain_info;
	std::vector<PVOID> call_chain_list;

	if (call_chain_length == 0) {
		printf("found unknown hash?? hash = %X, head = %X, ptr = %p\n", Hash, Head.descriptor, lpCallChain);
	}

	EnterCriticalSection(&this->call_chain_lock);

	chain_info.head = this->compressed_call_chains.size();
	for (i = 0; i < call_chain_length; i++) 
	{
		if (this->is_wow64) {
			chain_value = (PVOID)*((DWORD*)lpCallChain + i);
		}
		else {
			chain_value = *((PVOID*)lpCallChain + i);
		}

		this->compressed_call_chains.push_back(chain_value);
	}
	chain_info.tail = this->compressed_call_chains.size();
	chain_info.ref = 1;

	this->hash_to_call_chain.emplace(Hash, chain_info);

	LeaveCriticalSection(&this->call_chain_lock);
}

/* 用于读取可选项 */
void LogReader::_record_log_block(PCHAR lpLogBlock)
{
	/* 获取日志头 */
	LogHead& log_head = *(LogHead*)lpLogBlock;
	lpLogBlock += sizeof(LogHead);

	/* 默认的无效值 */
	LogDataC log_c;
	log_c.trap_id = log_head.trap_id;
	log_c.thread_index = -1;
	log_c.call_hash = -1;

	/* 读取线程ID，尝试进行压缩 */
	if (log_head.descriptor & LH_Mask_ThreadId) {

		DWORD thread_id;
		thread_id = *(DWORD*)lpLogBlock;
		lpLogBlock += sizeof(DWORD);

		EnterCriticalSection(&this->thread_index_lock);
		for (log_c.thread_index = 0; log_c.thread_index < this->thread_id_to_index.size(); log_c.thread_index++)
		{
			if (this->thread_id_to_index[log_c.thread_index] == thread_id)
				break;
		}

		if (log_c.thread_index == this->thread_id_to_index.size()) {
			this->thread_id_to_index.push_back(thread_id);
		}
		LeaveCriticalSection(&this->thread_index_lock);
	}

	/* 哈希调用链处理 */
	if (log_head.descriptor & LH_Mask_Omitted ||
		log_head.descriptor & LH_Mask_CallChainLength) {
		
		log_c.call_hash = *(DWORD*)lpLogBlock;
		lpLogBlock += sizeof(DWORD);

		this->_record_hash(log_head, log_c.call_hash, lpLogBlock);
	}

	EnterCriticalSection(&this->log_lock);
	this->compressed_log_records.push_back(log_c);
	LeaveCriticalSection(&this->log_lock);
}

/* 清空本地存储：日志、调用哈希 */
void LogReader::clear()
{
	EnterCriticalSection(&this->log_lock);
	this->hash_to_call_chain.clear();
	this->compressed_call_chains.clear();
	this->compressed_log_records.clear();
	LeaveCriticalSection(&this->log_lock);
}

/* 重启线程，可以选择是否清空远端的残留的日志记录（线程运行时依然可以重启） */
bool LogReader::start(bool NeedFlush)
{
	if (NeedFlush)
		this->flush_request = true;

	return ResumeThread(this->h_reader) != -1;
}

/* 暂停线程，可选择是否暂停服务端的日志记录 */
void LogReader::stop(bool NeedSkipLogging)
{
	if (NeedSkipLogging)
		this->skip_logging_request = true;

	this->pause_request = true;
}

/* 通过哈希查询函数调用链，返回值 = 引用次数 */
bool LogReader::query_hash(DWORD hash, CallChainInfo& call_chain)
{
	bool query_result = false;

	EnterCriticalSection(&this->call_chain_lock);

	auto ite_hash = this->hash_to_call_chain.find(hash);
	if (ite_hash != this->hash_to_call_chain.end()) {
		call_chain.list.assign(
			this->compressed_call_chains.begin() + ite_hash->second.head,
			this->compressed_call_chains.begin() + ite_hash->second.tail);

		call_chain.ref = ite_hash->second.ref;
		query_result = true;
	}

	LeaveCriticalSection(&this->call_chain_lock);

	return query_result;
}

/* 解压并复制到指定列表中（不会预清空列表） */
unsigned int LogReader::dupe_log_record(std::vector<LogData>& recv_list, DWORD start_rva, DWORD dupe_count)
{
	unsigned int i;
	unsigned int duped_count = 0;
	LogData log;

	EnterCriticalSection(&this->log_lock);
	EnterCriticalSection(&this->thread_index_lock);

	for (i = start_rva; i < start_rva + dupe_count; i++) {

		if (i >= this->compressed_log_records.size())
			break;

		LogDataC& log_c = this->compressed_log_records[i];
		log.trap_id = log_c.trap_id;
		if (log_c.thread_index < this->thread_id_to_index.size()) {
			log.thread_id = this->thread_id_to_index[log_c.thread_index];
		}
		else
			log.thread_id = -1;
		log.call_hash = log_c.call_hash;
		recv_list.push_back(log);

		duped_count++;
	}

	LeaveCriticalSection(&this->thread_index_lock);
	LeaveCriticalSection(&this->log_lock);

	return duped_count;
}

void LogReader::set_trace(WORD TrapId, bool Enable)
{
	if (TrapId >= MaxSlotCount)
		return;

	if ((this->flag_page[TrapId] & TF_Using) == false)
		return;

	if (Enable)
		this->flag_page[TrapId] |= TF_NeedTrace;
	else
		this->flag_page[TrapId] &= ~TF_NeedTrace;
}

bool LogReader::is_tracing(WORD TrapId)
{
	return this->flag_page[TrapId] & TF_NeedTrace;
}

/* 处理线程 */
DWORD WINAPI _reader_thread_func(LogReader* lpThis)
{
	LogHead* p_log;
	PCHAR p_to_read;

	// DEBUG
	bool is_full = false;
	unsigned int i = 0;

	while (1) {

		if (lpThis->dead_request) {
			ExitThread(0);
		}

		if (lpThis->flush_request) {
			lpThis->flush_request = false;
			lpThis->logger->head = lpThis->logger->tail;
		}

		if (lpThis->pause_request) {
			lpThis->pause_request = false;

			if (lpThis->skip_logging_request) {
				lpThis->skip_logging_request = false;
				lpThis->logger->skip_log_flag = true;
			}

			SuspendThread(GetCurrentThread());

			if (lpThis->logger->skip_log_flag) {
				lpThis->logger->skip_log_flag = false;
			}
		}

		/* 处理日志溢出 */
		if (lpThis->compressed_log_records.size() >= MaxLogRecordSize) {

			if (is_full == false) {
				printf("<r> I'm full, stop reading now\n");
				is_full = true;
			}
			Sleep(300);
			continue;
		}

		/* 正常工作 */
		p_to_read = lpThis->_read();
		if (p_to_read) {
			lpThis->_record_log_block(p_to_read);
			//printf("%d - %p\n", i++, p_to_read);
		}
		lpThis->_read_done();

		if (p_to_read == NULL) {
			Sleep(30);
		}
	}

	return -1;
}

unsigned int LogReader::log_count()
{
	return this->compressed_log_records.size();
}

void LogReader::print_log_data_info()
{
	unsigned int log_record_count = this->compressed_log_records.size();
	unsigned int hash_entry_count = this->hash_to_call_chain.size();
	unsigned int hash_chain_size = this->compressed_call_chains.size();
	double hash_log_rate = (double)hash_entry_count / (double)log_record_count;
	double average_hash_chain_size = (double)hash_chain_size / (double)hash_entry_count;

	printf("log data info:\n");
	printf("----------------------------\n");
	printf("log record count: %d\n", log_record_count);
	printf("dropped log count: %d\n", this->logger->dropped);
	printf("sent log count: %d\n", this->logger->sent);
	printf("hash entry count: %d\n", hash_entry_count);
	printf("hash / log rate = %.2lf\n", hash_log_rate);
	printf("average hash chain size = %.2lf\n", average_hash_chain_size);
	printf("\n");
}