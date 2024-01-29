#include "framework.h"
#include <vector>
#include <tlhelp32.h>
#include <winternl.h>
#include <windows.h>

/*

	本文件作用——处理客户端的HOOK的相关请求

*/

#define TrapSize 12

// 共享特殊内存区
MySharedFileHead* s_p_shared_memory = NULL;

static MainBrain* s_main_brain = NULL;
static BYTE* s_flag_page = NULL;
static PVOID* s_address_page = NULL;
static DWORD* s_count_page = NULL;

extern bool g_process_is_exiting;

// 与汇编文件共享的变量，不与其他文件共享。
extern "C" {
	extern void trap_arrays();

	DWORD watch_privilege_process_id = 0;
	DWORD watch_privilege_thread_id = 0;

	PVOID lp_flag_page_for_asm = NULL;
	PVOID lp_address_page_for_asm = NULL;
	PVOID lp_count_page_for_asm = NULL;
}

static std::vector<HANDLE> s_thread_list;

// 更新线程，仅有的可能的错误——内存不足
static void update_threads_for_detour()
{
	/* 释放原有的 */
	auto ite = s_thread_list.begin();
	while (ite != s_thread_list.end()) {
		CloseHandle(*ite);
		ite++;
	}

    HANDLE h_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (h_snapshot == INVALID_HANDLE_VALUE) {
        errln_ex("Create tool help snapshot failed..");
        return;
    }

    THREADENTRY32 te32;
    te32.dwSize = sizeof(THREADENTRY32);

    if (!Thread32First(h_snapshot, &te32)) {
        errln_ex("Failed to get first thread snapshot..");
        goto CLEAN_AND_EXIT;
    }

	HANDLE hThread;
    do {
        if (te32.th32OwnerProcessID == GetCurrentProcessId() &&
            te32.th32ThreadID != GetCurrentThreadId()) {

            hThread = OpenThread(THREAD_ALL_ACCESS, false, te32.th32ThreadID);
            if (hThread == NULL) {
                errln_ex("Failed to open thread");
                continue;
            }

			DetourUpdateThread(hThread);

			s_thread_list.push_back(hThread);
        }

    } while (Thread32Next(h_snapshot, &te32));

CLEAN_AND_EXIT:
    CloseHandle(h_snapshot);
}

/*

	FLAG PAGE 用法：
	
	0. “未使用”状态
		所有的位为空，即byte值为0

	1. “正常”状态
		FM_Vaild 表示槽位的已钩/未钩（由Server设置）

	2. “转换中”状态
		FM_ChangeRequest 表示钩的请求（由Client设置）
		结合 FM_Vaild 表示将要进行的操作

	3. “异常”状态
		FM_Error 表示状态转变错误（由Server设置）
		此时的 FM_Vaild 表示转变前的状态
		FM_ChangeRequest 此时无效

	HOOK 机制：
	1. 只要有一个槽位失败，本次操作会全部中止。
	2. 其中一个任务的失败不会影响剩余任务的尝试。
	3. 能够过滤出异常的指针（不可访问、不可执行和读取）
*/
static void worker_handle_hooks()
{
	bool need_abort = false;
	int i;
	LONG commit_result, swap_result = 0;
	MEMORY_BASIC_INFORMATION mbi;

	//s_main_brain->detour_error_count = 0;

	DetourTransactionBegin();

	for (i = 0; i < MaxSlotCount; i++)
	{
		if (s_flag_page[i] & TF_Error)
			continue;

		/* 无请求 */
		if (!(s_flag_page[i] & TF_ChangeRequest))
			continue;

		/* 检查是否存在异常指针，如果有，需要中止排队中的所有任务 */
		if (!VirtualQuery(s_address_page[i], &mbi, sizeof(mbi))) {	// 不可访问区（非用户空间）
			//infoln("failed to query, address = %p", s_address_page[i]);
			s_flag_page[i] |= TF_Error;
		}
		else if (!(mbi.Protect & PAGE_EXECUTE_READ) &&	// 非代码区（非ER内存页）
			!(mbi.Protect & PAGE_EXECUTE_WRITECOPY) &&
			!(mbi.Protect & PAGE_EXECUTE_READWRITE)) {
			//infoln("not-code, address = %p", s_address_page[i]);
			s_flag_page[i] |= TF_Error;
		}

		if (s_flag_page[i] & TF_Error) {
			need_abort = true;
			// infoln("bad address: %p", s_address_page[i]);
			continue;
		}

		/* 脱钩/挂钩任务排队 */
		if (s_flag_page[i] & TF_Using) {
			swap_result = DetourDetach(s_address_page + i, (PUCHAR)trap_arrays + TrapSize * i);
			if (swap_result != NO_ERROR) {
				s_flag_page[i] |= TF_Error;
				// errln("failed to unhook! Address = %p", *(s_address_page + i));
				need_abort = true;
				DetourTransactionAbort();
				DetourTransactionBegin();
			}
		}
		else {
			swap_result = DetourAttach(s_address_page + i, (PUCHAR)trap_arrays + TrapSize * i);
			if (swap_result != NO_ERROR) {
				s_flag_page[i] |= TF_Error;
				// errln("failed to hook! Address = %p, result = %d", *(s_address_page + i), swap_result);
				need_abort = true;
				DetourTransactionAbort();
				DetourTransactionBegin();
			}
		}
	}

	if (need_abort) {
		DetourTransactionAbort();
		commit_result = !NO_ERROR;
	}
	else {
		update_threads_for_detour();
		commit_result = DetourTransactionCommit();
	}

	if (commit_result == NO_ERROR) {
		for (i = 0; i < MaxSlotCount; i++) {
			if (s_flag_page[i] & TF_ChangeRequest) {
				s_flag_page[i] &= ~TF_ChangeRequest;
				s_flag_page[i] ^= TF_Using;

				//if (s_flag_page[i] & TF_Using)
				//	infoln("%d - valid!", i);
				//else
				//	infoln("%d - invalid!", i);

				/* 注销陷阱项，需要额外的操作 */
				if ((s_flag_page[i] & TF_Using) == false) {
					s_flag_page[i] = 0;
					s_count_page[i] = 0;	/* 清空计数 */
				}
			}
		}

	}
	else {
		/* 不更新表，因为没有操作实现，并且已经给出了错误 */
		//infoln("failed!");
	}
}

static HANDLE s_reply_event;
HANDLE g_request_event;

extern bool g_process_is_exiting;
extern DWORD g_worker_id;

void init_my_trace(LoggerBrain& Brain, PVOID pLogBuffer);
void uninit_my_trace();

/* 创建的工作线程，由它实现整个例程的初始化 */
DWORD WINAPI kz_init_worker_thread(MyTempSection* lpTempSection)
{
	/* 尝试连接控制台 */
	bool console_attached = false;
	if (lpTempSection->client_id != GetCurrentProcessId()) {
		FILE* fp;
		if (!AttachConsole(lpTempSection->client_id)) {
			MessageBoxA(NULL, "failed to attach to the console", "ERROR", MB_ICONERROR);
		}
		else {
			freopen_s(&fp, "CONOUT$", "w+t", stdout);
			infoln("hello world!");
			console_attached = true;
		}
	}

	/* 打开共享区段 */
	HANDLE h_mapping = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, false, lpTempSection->shared_file_name);
	if (!h_mapping) {
		errln_ex("failed to open mapped file");
		//printf("\tmapped file name = %s\n", lpTempSection->shared_file_name);
		return -1;
	}

	/* 映射共享区段 */
	s_p_shared_memory = (MySharedFileHead*)MapViewOfFile(h_mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
	if (s_p_shared_memory == NULL) {
		errln_ex("failed to map the file into this process");
		return -2;
	}

	/* 初始化同步事件 */
	s_reply_event = OpenEventA(EVENT_ALL_ACCESS, false, lpTempSection->reply_event_name);
	g_request_event = OpenEventA(EVENT_ALL_ACCESS, false, lpTempSection->request_event_name);
	if (s_reply_event == NULL || g_request_event == NULL) {
		errln_ex("failed to open worker event");
		return -3;
	}

	/* 填写本模块所必须的信息 */
	s_main_brain = &s_p_shared_memory->main;
	s_flag_page = (BYTE*)(s_main_brain->flag_page_rva + (PCHAR)s_p_shared_memory);
	s_address_page = (PVOID*)(s_main_brain->address_page_rva + (PCHAR)s_p_shared_memory);
	s_count_page = (DWORD*)(s_main_brain->count_page_rva + (PCHAR)s_p_shared_memory);

	/* 填充ASM所需信息 */
	watch_privilege_thread_id = GetCurrentThreadId();
	lp_flag_page_for_asm = s_flag_page;
	lp_address_page_for_asm = s_address_page;
	lp_count_page_for_asm = s_count_page;

	/* 尝试初始化日志记录 */
	if (s_p_shared_memory->config.is_logger_enabled) {
		init_my_trace(s_p_shared_memory->logger, s_main_brain->log_page_rva + (PCHAR)s_p_shared_memory);
	}

	// infoln("trap array: %p", trap_arrays);

	SetEvent(s_reply_event);

	/* 开始工作。。 */
	DWORD wait_result;
	while (1) {
		
		/* 等待。特殊情况：客户端崩溃？不影响运行。 */
		wait_result = WaitForSingleObject(g_request_event, INFINITE);
		if (wait_result != WAIT_OBJECT_0) {
			errln_ex("request event is wrong?");
		}

		/* 被动退出 */
		if (s_main_brain->close_worker_request)
			break;

		/* 主动退出 */
		if (s_main_brain->close_worker_request)
			break;

		/* 工作 */
		worker_handle_hooks();

		/* 报告 */
		SetEvent(s_reply_event);
	}

	/* 清理工作：1. 脱钩 */
	int i;
	for (i = 0; i < MaxSlotCount; i++)
	{
		if (s_flag_page[i] & TF_Using)
			s_flag_page[i] |= TF_ChangeRequest;
	}
	worker_handle_hooks();

	/* 2.等待卸载 */
	Sleep(100);

	/* 3.卸载追踪 */
	uninit_my_trace();

	/* 4.删除事件 */
	CloseHandle(s_reply_event);
	CloseHandle(g_request_event);

	/* 取消映射 */
	UnmapViewOfFile(s_p_shared_memory);
	CloseHandle(h_mapping);

	/* 断开控制台 */
	if (console_attached) {
		FreeConsole();
	}

	return 0;
}