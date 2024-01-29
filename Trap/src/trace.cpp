#include "framework.h"
#include <unordered_set>

/*

	此文件作用——负责在内存中保存高级输出日志

*/

typedef struct
{
	DWORD eax;
	DWORD ebx;
	DWORD ecx;
	DWORD edx;
	DWORD esi;
	DWORD edi;
	DWORD esp;
	DWORD ebp;

	DWORD eflags;

	DWORD eip;
	DWORD id;

}MyTrapFrame32;

typedef struct
{
	DWORD64 rax;
	DWORD64 rbx;
	DWORD64 rcx;
	DWORD64 rdx;
	DWORD64 rsi;
	DWORD64 rdi;
	DWORD64 rsp;
	DWORD64 rbp;

	DWORD64 r8;
	DWORD64 r9;
	DWORD64 r10;
	DWORD64 r11;
	DWORD64 r12;
	DWORD64 r13;
	DWORD64 r14;
	DWORD64 r15;

	DWORD64 rflags;

	DWORD64 rip;
	DWORD64 id;

}MyTrapFrame64;

#ifdef _WIN64
typedef MyTrapFrame64 MyTrapFrame;
#else
typedef MyTrapFrame32 MyTrapFrame;
#endif

/* 重要的外部信号 */
extern bool g_process_is_exiting;

/* 核心控制区 */
static LONG s_log_lock = 1;
static int s_shadow_tail = 0;
static LoggerBrain* s_logger = NULL;
static PCHAR s_log_page = NULL;
static PVOID s_back_trace_buffer[LH_Mask_CallChainLength];

/* 特殊模块 */
std::unordered_set<DWORD>* s_hash_set;

static unsigned int get_log_block_size(LogHead& Head)
{
	unsigned char call_chain_length = 0;
	unsigned int log_block_size = sizeof(LogHead);

	if (Head.descriptor & LH_Mask_ThreadId)
		log_block_size += sizeof(DWORD);

	call_chain_length = Head.descriptor & LH_Mask_CallChainLength;
	if (call_chain_length)
		log_block_size += sizeof(DWORD) + call_chain_length * sizeof(PVOID);
	else if (Head.descriptor & LH_Mask_Omitted)
		log_block_size += sizeof(DWORD);

	return log_block_size;
}
/*

	头指针 -> 首个有效元素
	尾指针 -> 首个无效字节
	头 = 尾 -> 代表没有元素
	指针偏移 < 容量边界

	缓冲区末端空隙处理方式：
		保留

	超载检测方式：
		尾指针 to 头指针的距离 <= 本次日志大小

	同步方式：
		抢锁 -> 检测是否超载 -> 写入 -> 设尾指针 -> 放锁
	（超载处理方式：丢掉）

	返回值：
		给出缓冲区指针，用于写入信息。
		NULL = 爆满，可能需要丢弃。

*/

static char* acquire_log_access_and_check(int LogSize)
{
	/* 自旋操作，抢锁 */
	while (!InterlockedExchange(&s_log_lock, 0));

	int cur_head = s_logger->head;
	int cur_tail = s_logger->tail;
	char* start_address;

	/* 
		不需要换行
		1. 头在尾后，且够大
		2. 头在尾前，且头到下边界之间够大
	*/
	if (cur_head - cur_tail > LogSize ||
		(s_logger->capability - cur_tail > LogSize && cur_head - cur_tail <= 0)){
		s_shadow_tail = LogSize + cur_tail;
		start_address = s_log_page + cur_tail;
	}
	/* 
		需要换行
		1. 头在尾前，且上边界到头之间够大
	*/
	else if (cur_head > LogSize && cur_head - cur_tail <= 0) {
		s_shadow_tail = LogSize;
		start_address = s_log_page;
		
		/* 末端日志头置零 */
		if (s_logger->capability - cur_tail >= sizeof(LogHead)) {
			ZeroMemory(s_log_page + cur_tail, sizeof(LogHead));
		}
	}
	/* 判定为超载 */
	else {
		s_shadow_tail = cur_tail;
		start_address = NULL;
	}

	if (start_address) {
		// infoln("<w> h: %d, t: %d -> %d", cur_head, cur_tail, s_shadow_tail);
	}

	return start_address;
}

static void commit_and_release_log_access()
{
	s_logger->tail = s_shadow_tail;
	s_log_lock = 1;
}

extern "C" {

	/*
		核心追踪函数
		1. 默认记录所有信息（包括完整的调用链）

	*/
	size_t my_trace_routine(MyTrapFrame* pTrapFrame)
	{
		WORD valid_count;
		LogHead log;
		DWORD my_hash;
		DWORD thread_id;
		DWORD log_block_size;
		PCHAR write_buffer;

		/* 进程正在退出 */
		if (g_process_is_exiting) {
			return 0;
		}

		/* 清除请求 */
		if (s_logger->clear_cache_flag == true) {
			s_hash_set->clear();
			s_logger->dropped = 0;
			s_logger->clear_cache_flag = false;
		}

		/* 跳过记录请求 */
		if (s_logger->skip_log_flag == true) {
			return 0;
		}

		log.descriptor = 0;
		log.trap_id = pTrapFrame->id;

		//if (log.trap_id > 0x1000) {
		//	infoln("warning! abnormal trap id = %d", log.trap_id);
		//}

		thread_id = GetCurrentThreadId();
		valid_count = RtlCaptureStackBackTrace(2, LH_Mask_CallChainLength, s_back_trace_buffer, &my_hash);	// --> 因为是单线程操作，可以使用静态存储

		//infoln("%d - %X", valid_count, my_hash);

		/* 构建日志描述符 */
		log.descriptor = LH_Mask_Valid;
		log.descriptor |= LH_Mask_ThreadId;
		if (s_hash_set->find(my_hash) != s_hash_set->end())
			log.descriptor |= LH_Mask_Omitted;
		else
			log.descriptor += valid_count;

		log_block_size = get_log_block_size(log);

		/* 尝试获取缓冲区信息 */
		write_buffer = acquire_log_access_and_check(log_block_size);

		/* 成功获取空闲区 */
		if (write_buffer) {

			//infoln("%p", write_buffer);

			/* 日志头 */
			*(LogHead*)write_buffer = log;
			write_buffer += sizeof(LogHead);

			/* 线程ID */
			if (log.descriptor & LH_Mask_ThreadId) {
				*(DWORD*)write_buffer = thread_id;
				write_buffer += sizeof(DWORD);
			}

			/* 哈希 + 完整调用链 */
			if (log.descriptor & LH_Mask_CallChainLength) {
				*(DWORD*)write_buffer = my_hash;
				write_buffer += sizeof(DWORD);
				memcpy(write_buffer, s_back_trace_buffer, valid_count * sizeof(PVOID));

				s_hash_set->insert(my_hash);
			}
			else if (log.descriptor & LH_Mask_Omitted) {
				*(DWORD*)write_buffer = my_hash;
				write_buffer += sizeof(DWORD);
			}

			s_logger->sent++;
			
		}
		/* 申请失败，可能需要丢弃此记录 */
		else {
			s_logger->dropped++;
		}

		/* 释放锁 */
		commit_and_release_log_access();

		return 0;
	}
}

/* 
	初始化追踪及日志记录
	—— 从何处读取，如何读取。
*/
void init_my_trace(LoggerBrain& Brain, PVOID pLogBuffer)
{
	s_logger = &Brain;
	s_log_page = (PCHAR)pLogBuffer;

	s_hash_set = new std::unordered_set<DWORD>;
}

/*
	去初始化：
	（暂不考虑）
*/
void uninit_my_trace()
{
	delete s_hash_set;
}

//// ============================== 测试专用函数 ============================= //
//
///*
//	生成随机日志头
//		陷阱ID（必选）、线程ID、调用堆栈哈希值、返回地址链
//*/
//void generate_random_log_head(LogHead* pHead)
//{
//	pHead->descriptor = LH_Mask_Valid;
//
//	/* 线程ID */
//	if (rand() % 2) {
//		pHead->descriptor |= LH_Mask_ThreadId;
//	}
//
//	/* 返回地址链 */
//	DWORD chain_length = rand() % 30 + 1;
//	if (chain_length) {
//		chain_length &= LH_Mask_CallChain;
//		pHead->descriptor += chain_length;
//	}
//}
//
//void write_random_data_by_log_head(PCHAR pWriteBuffer, LogHead* pHead)
//{
//	/* 日志头 */
//	pHead->trap_id = rand() % 0x1000;
//	*(LogHead*)pWriteBuffer = *pHead;
//	pWriteBuffer += sizeof(LogHead);
//
//	/* 线程ID */
//	if (pHead->descriptor & LH_Mask_ThreadId) {
//		DWORD thread_id = GetCurrentThreadId();
//		*(DWORD*)pWriteBuffer = thread_id;
//		pWriteBuffer += sizeof(DWORD);
//	}
//
//	/* 函数调用链 */
//	DWORD retn_chain_length = pHead->descriptor & LH_Mask_CallChain;
//	if (retn_chain_length) {
//		DWORD my_hash = rand();
//		*(DWORD*)pWriteBuffer = my_hash;
//		pWriteBuffer += sizeof(DWORD);
//
//		while (retn_chain_length) {
//
//			*(PVOID*)pWriteBuffer = (PVOID)rand();
//			pWriteBuffer += sizeof(PVOID);
//			retn_chain_length--;
//		}
//	}
//	
//}