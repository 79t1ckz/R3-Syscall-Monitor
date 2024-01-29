#include "../include/client.h"

/*
	LogReader 读写测试：
	1. 写入方：多个线程
	2. 读取方：单个LogReader模块所属线程

	测试用例：
	Head.Descriptor - 随机表示
	Head.TrapId - [0, 0x1000)的遍历值
	ThreadId - 0x2000 + [0, 10)的随机值
	CallHash - 0x8000 + [1, 32)的随机值
	CallChain - 0x7F000000 + [0, 31)的随机值
*/

#define TestLogBufferSize 0x8000

static WORD g_cur_trap_id = 0;
static LoggerBrain g_brain;
static PVOID g_log_buffer = NULL;
static PCHAR g_log_flag_page = NULL;

// ========================== 写入方 ======================== //
void init_my_trace(LoggerBrain& Brain, PVOID pLogBuffer);
unsigned int get_log_block_size(LogHead& Head);
char* acquire_log_access_and_check(int LogSize);
void commit_and_release_log_access();

/* 随机生成日志头 */
void spawn_log_head(LogHead& Head)
{
	Head.trap_id = -1;
	Head.descriptor = LH_Mask_Valid;

	if (rand() % 2)
		Head.descriptor |= LH_Mask_ThreadId;

	Head.descriptor += rand() % 32;
}

void write_log_block(LogHead& Head, PCHAR pWriteBuffer)
{
	WORD call_chain_length;
	DWORD thread_base_id = 0x2000;
	DWORD hash_base_value = 0x8000;
	PCHAR call_chain_base_address = (PCHAR)0x7F000000;

	/* 写入操作 */
	Head.trap_id = g_cur_trap_id++;
	*(LogHead*)pWriteBuffer = Head;
	pWriteBuffer += sizeof(LogHead);

	/* 线程ID */
	if (Head.descriptor & LH_Mask_ThreadId) {
		*(DWORD*)pWriteBuffer = thread_base_id + rand() % 10;
		pWriteBuffer += sizeof(DWORD);
	}

	/* 调用链 */
	call_chain_length = Head.descriptor & LH_Mask_CallChain;
	if (call_chain_length) {
		*(DWORD*)pWriteBuffer = hash_base_value + call_chain_length;
		pWriteBuffer += sizeof(DWORD);

		for (int i = 0; i < call_chain_length; i++) {
			*(PVOID*)pWriteBuffer = call_chain_base_address + i;
			pWriteBuffer += sizeof(PVOID);
		}
	}
}

DWORD WINAPI test_writer_thread(LPVOID lParam)
{
	bool is_dropping_log_block = false;

	DWORD thread_id = GetCurrentThreadId();
	unsigned int log_block_size;
	LogHead log_head;
	PCHAR to_write_buffer;

	infoln("<w> woke up !");

	while (1) 
	{
		/* 生成测试用例 */
		spawn_log_head(log_head);
		log_block_size = get_log_block_size(log_head);
		// infoln("<w> desc = %X, size = %d", log_head.descriptor, log_block_size);

		/* 尝试写入 */
		to_write_buffer = acquire_log_access_and_check(log_block_size);
		if (to_write_buffer) {
			if (is_dropping_log_block == true) {
				infoln("<w - %d> started logging.. again ? success size = %d", thread_id, log_block_size);
				is_dropping_log_block = false;
			}
			write_log_block(log_head, to_write_buffer);
		}
		else {
			if (is_dropping_log_block == false) {
				infoln("<w - %d> buffer is full, now drop log blocks.. failed size = %d", thread_id, log_block_size);
				is_dropping_log_block = true;
			}
			Sleep(1000);
		}
		commit_and_release_log_access();

		Sleep(30);
	}

	return 0;
}

// ========================== 读取方 ======================== //
extern LogReader* g_reader;


// ========================== 测试用 ======================== //

void print_log_entry(int start_rva)
{
	if (g_reader == NULL) {
		errln("log reader haven't inited!");
		return;
	}

	std::vector<LogData> dup_list;
	g_reader->dupe_log_record(dup_list, start_rva, 100);

	if (dup_list.empty()) {
		errln("* no log entrys at [%d, %d) *", start_rva, start_rva + 100);
		return;
	}

	infoln("\tindex\trap_id\tthread_id\thash");
	infoln("-------------------------------------------------");
	int i = 0;
	for (; i < dup_list.size(); i++)
	{
		printf("\t%d", i + start_rva);
		printf("\t%X", dup_list[i].trap_id);

		if (dup_list[i].thread_id == -1)	// 可能未知
			printf("\t( ? )");
		else
			printf("\t%X", dup_list[i].thread_id);

		if (dup_list[i].call_hash == -1)	// 可能不存在
			printf("\t( \\ )");
		else
			printf("\t%X", dup_list[i].call_hash);
		printf("\n");
	}
}

void print_call_chain_by_hash(DWORD hash)
{
	std::vector<PVOID> call_chain_list;

	if (g_reader->query_hash(hash, call_chain_list)) {
		infoln("\rcall chain:");
		infoln("------------------------------");
		for (int i = 0; i < call_chain_list.size(); i++) {
			infoln("\t%p", call_chain_list[i]);
		}
	}
	else {
		infoln("* unknown hash ?? *");
	}
}

static char g_p_menu[] = R"EOF(

Test for log reader
------------------------------
1 - read 100 entrys of the log
2 - query hash
3 - restart
4 - pause

)EOF";

void test_shell()
{
	int command;
	int param;

	printf("%s", g_p_menu);

	while (1) {
		
		scanf_s("%d", &command);
		rewind(stdin);

		if (!command)
			break;

		system("cls");
		switch (command) {
		case 1:
			infoln("from where ?");
			scanf_s("%d", &param);
			rewind(stdin);
			print_log_entry(param);
			break;

		case 2:
			infoln("which hash ?");
			scanf_s("%X", &param);
			rewind(stdin);
			print_call_chain_by_hash(param);
			break;

		case 3:
			infoln("need to flush buffer? - (0/1)");
			scanf_s("%d", &param);
			rewind(stdin);
			g_reader->start(param);
			infoln("started!");
			break;

		case 4:
			g_reader->stop();
			infoln("paused!");
			break;

		default:
			printf("%s", g_p_menu);
			break;
		}
	}
}

int main()
{
	g_log_buffer = VirtualAlloc(NULL, TestLogBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	g_brain.capability = TestLogBufferSize;
	g_brain.dropped = 0;
	g_brain.head = 0;
	g_brain.tail = 0;

	init_my_trace(g_brain, g_log_buffer);

	g_reader = new LogReader(g_log_buffer, g_log_flag_page, g_brain, false);
	g_reader->start();

	HANDLE h_writer_0 = CreateThread(NULL, 0, test_writer_thread, NULL, 0, NULL);
	HANDLE h_writer_1 = CreateThread(NULL, 0, test_writer_thread, NULL, 0, NULL);
	HANDLE h_writer_2 = CreateThread(NULL, 0, test_writer_thread, NULL, 0, NULL);
	HANDLE h_writer_3 = CreateThread(NULL, 0, test_writer_thread, NULL, 0, NULL);

	test_shell();

	return 0;
}