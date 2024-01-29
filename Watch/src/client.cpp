#include "../include/client.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <windows.h>


#define MY_DLL_NAME_32	L"Trap32.dll"
#define MY_DLL_NAME_64	L"Trap64.dll"

/* 模块解析 */
ModuleParser* g_parser = NULL;
/* 操作对象 */
TrapManager* g_manager = NULL;
FastCounter* g_counter = NULL;
LogReader* g_reader = NULL;

/* 模块加载 */
static MyTempSection* s_temp_loader_section = NULL;
/* 目标设置 */
static bool s_is_wow_64 = false;
static DWORD s_target_id = -1;		// 用于标识是否已经成功初始化
static HANDLE s_target = NULL;
/* 文件映射 */
static MySharedFileHead* s_my_shared_memory = NULL;
static HANDLE s_my_mapped_file = NULL;
/* 同步事件 */
static HANDLE s_request_event = NULL;
static HANDLE s_reply_event = NULL;
/* 工作线程管理 */
static HANDLE s_worker = NULL;
static LPTHREAD_START_ROUTINE s_r_worker_entry_point = NULL;

/* 路径设置 */
static wchar_t s_my_dll_path_32[MAX_PATH] = { 0 };
static wchar_t s_my_dll_path_64[MAX_PATH] = { 0 };
/* 名称设置 */
static char s_shared_file_name[MAX_PATH] = { 0 };
static char s_request_event_name[MAX_PATH] = { 0 };
static char s_reply_event_name[MAX_PATH] = { 0 };

/*
	清除目标：
	1. 析构解析器
	2. 关闭句柄
	——尽量保持目标进程正常运作
*/
static void unset_my_target()
{
	if (s_temp_loader_section) {
		VirtualFreeEx(s_target, s_temp_loader_section, 0, MEM_RELEASE);
		s_temp_loader_section = NULL;
	}

	if (g_parser) {
		delete g_parser;
		g_parser = NULL;
	}

	if (s_target) {
		CloseHandle(s_target);
		s_target = NULL;
	}

	s_target_id = -1;
}

/*
	目标
	1. 创建进程解析器
	2. 注入DLL
	3. 搜索导出函数
*/
static bool set_my_target(DWORD proc_id)
{
	BOOL write_result = FALSE;
	ModuleInfo* p_exe_info = NULL;
	// PVOID lp_data_section = NULL;
	HANDLE h_dll_loader = NULL;
	ModuleInfo* p_kernel32 = NULL;
	LPTHREAD_START_ROUTINE p_dll_loader = NULL;
	ModuleInfo* p_my_dll = NULL;

	s_target_id = proc_id;

	/* 打开句柄 */
	s_target =
		OpenProcess(
			PROCESS_CREATE_THREAD | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION,
			false,
			proc_id
		);
	if (s_target == NULL) {
		errln_ex("failed to open process..");
		goto ERROR_EXIT;
	}

	/* 生成解析器 */
	g_parser = new ModuleParser(s_target);
	g_parser->walkAddressSpace();
	p_exe_info = g_parser->exe_info();
	if (p_exe_info == NULL) {
		errln_ex("can't find the exe module of process");
		goto ERROR_EXIT;
	}

	s_is_wow_64 = p_exe_info->pe_type == 32;

	/* 创建DLL加载线程，等待其完成任务 */
	p_kernel32 = g_parser->queryModule(L"kernel32.dll");
	p_dll_loader = (LPTHREAD_START_ROUTINE)g_parser->getProcAddr(p_kernel32, "LoadLibraryW", NULL);
	if (p_dll_loader == NULL) {
		errln_ex("can't find kernel32!LoadLibraryW");
		goto ERROR_EXIT;
	}

	s_temp_loader_section = (MyTempSection*)VirtualAllocEx(s_target, NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (s_temp_loader_section == NULL) {
		errln_ex("can't alloc a data section");
		goto ERROR_EXIT;
	}

	if (s_is_wow_64)
		write_result = WriteProcessMemory(s_target, s_temp_loader_section, s_my_dll_path_32, sizeof(s_my_dll_path_32), NULL);
	else
		write_result = WriteProcessMemory(s_target, s_temp_loader_section, s_my_dll_path_64, sizeof(s_my_dll_path_64), NULL);
	if (write_result == FALSE) {
		errln_ex("failed to write param..");
		VirtualFreeEx(s_target, s_temp_loader_section, 0, MEM_RELEASE);
		goto ERROR_EXIT;
	}

	h_dll_loader =
		CreateRemoteThread(
			s_target, 
			NULL,
			0,
			p_dll_loader,
			s_temp_loader_section,
			0,
			NULL);
	if (h_dll_loader == NULL) {
		errln_ex("failed to create dll loader..");
		goto ERROR_EXIT;
	}

	if (WaitForSingleObject(h_dll_loader, 5000) != WAIT_OBJECT_0) {
		errln_ex("something is wrong with dll loader ?");
		CloseHandle(h_dll_loader);
		goto ERROR_EXIT;
	}

	/* 清理加载器 */
	CloseHandle(h_dll_loader);

	/* 搜索导出函数 */
	g_parser->walkAddressSpace();
	if (s_is_wow_64)
		p_my_dll = g_parser->queryModule(MY_DLL_NAME_32);
	else
		p_my_dll = g_parser->queryModule(MY_DLL_NAME_64);
	if (p_my_dll == NULL) {
		errln("failed to load my dll, please check:");
		wprintf(L"\t32-bit path = %s\n", s_my_dll_path_32);
		wprintf(L"\t64-bit path = %s\n", s_my_dll_path_64);
		goto ERROR_EXIT;
	}

	s_r_worker_entry_point = (LPTHREAD_START_ROUTINE)g_parser->getProcAddr(p_my_dll, "kz_init_worker_thread", NULL);
	if (s_r_worker_entry_point == NULL) {
		errln("can't find trap dll's export worker thread funtion");
		goto ERROR_EXIT;
	}

	return true;

	/* 异常退出 */
ERROR_EXIT:
	return false;
}

static void uninit_my_shared_memory()
{
	if (s_my_shared_memory) {
		UnmapViewOfFile(s_my_shared_memory);
		s_my_shared_memory = NULL;
	}
	if (s_my_mapped_file) {
		CloseHandle(s_my_mapped_file);
		s_my_mapped_file = NULL;
	}
}

/* 
	初始化要求：
	1. 填写完控制页
	2. （疑问：共享内存是否会自动置零？）
*/
static bool init_my_shared_memory(MyConfig& Config)
{
	/* 控制区 + 标志区 + 计数区 + 插桩地址区 （+ 日志区） */
	DWORD control_page_size = MaxSlotCount;	
	DWORD flag_page_size = MaxSlotCount * sizeof(BYTE);
	DWORD count_page_size = MaxSlotCount * sizeof(DWORD);
	DWORD address_page_size = Config.is_wow64 ? MaxSlotCount * sizeof(DWORD) : MaxSlotCount * sizeof(PVOID);
	DWORD log_page_size = Config.is_logger_enabled ? LogPageSize : 0;
	DWORD mapped_file_size = 
			control_page_size +
			flag_page_size +
			count_page_size + 
			address_page_size +
			log_page_size;

	/* 创建映射文件对象 */
	s_my_mapped_file = 
		CreateFileMappingA(
			INVALID_HANDLE_VALUE,
			NULL,
			PAGE_READWRITE, 
			0, 
			mapped_file_size,
			s_shared_file_name
		);

	if (s_my_mapped_file == NULL) {
		errln_ex("failed to create file mapping");
		return false;
	}

	/* 映射文件到本进程 */
	s_my_shared_memory =
		(MySharedFileHead*)MapViewOfFile(
			s_my_mapped_file,
			FILE_MAP_ALL_ACCESS,
			0,
			0,
			0
		);

	if (s_my_shared_memory == NULL) {
		errln_ex("failed to mapped file into this process..");
		return false;
	}

	s_my_shared_memory->config = Config;

	MainBrain& p_main = s_my_shared_memory->main;
	p_main.flag_page_rva = control_page_size;
	p_main.count_page_rva = p_main.flag_page_rva + flag_page_size;
	p_main.address_page_rva = p_main.count_page_rva + count_page_size;
	p_main.log_page_rva = Config.is_logger_enabled ? p_main.address_page_rva + address_page_size : 0;

	//infoln("Inited shared memory:");
	//infoln("log page = %p", p_main.log_page_rva + (PCHAR)s_my_shared_memory);

	if (Config.is_logger_enabled) {
		LoggerBrain& p_logger = s_my_shared_memory->logger;
		p_logger.capability = log_page_size;
	}

	return true;
}

void print_my_shared_memory()
{
	if (s_my_shared_memory == NULL) {
		infoln("* Not Mapped *");
		return;
	}

	infoln("Shared Memory - %p", s_my_shared_memory);
	infoln("\tControl rva: +0x0");
	infoln("\tFlag page rva: +0x%X", s_my_shared_memory->main.flag_page_rva);
	infoln("\tCount page rva: +0x%X", s_my_shared_memory->main.count_page_rva);
	infoln("\tAddress page rva: +0x%X", s_my_shared_memory->main.address_page_rva);

	if (s_my_shared_memory->config.is_logger_enabled)
		infoln("\tLog page rva: +0x%X", s_my_shared_memory->main.log_page_rva);
}

/* 销毁所有成员 */
static void uninit_my_core()
{
	/* 模块 */
	if (g_manager) {
		delete g_manager;
		g_manager = NULL;
	}
	if (g_counter) {
		delete g_counter;
		g_counter = NULL;
	}
	if (g_reader) {
		delete g_reader;
		g_reader = NULL;
	}

	/* 远端工作线程 */
	if (s_worker) {
		s_my_shared_memory->main.close_worker_request = true;
		SetEvent(s_request_event);
		WaitForSingleObject(s_worker, 1000);
		CloseHandle(s_worker);
		s_worker = NULL;
	}

	/* 同步事件 */
	if (s_reply_event) {
		CloseHandle(s_reply_event);
		s_reply_event = NULL;
	}
	if (s_request_event) {
		CloseHandle(s_request_event);
		s_request_event = NULL;
	}
}

/*
	初始化核心模块
	前置条件：
		1. 已设置目标（构建解析器、注入DLL、搜索导出函数）
		2. 映射共享内存（初始化基本数据）
*/
static bool init_my_core(MyConfig& Config)
{
	DWORD controller_id;
	PCHAR base_address;

	/* 填写基本信息 */
	controller_id = GetCurrentProcessId();
	if (WriteProcessMemory(s_target, &s_temp_loader_section->client_id, &controller_id, sizeof(DWORD), NULL);
		WriteProcessMemory(s_target, &s_temp_loader_section->shared_file_name, s_shared_file_name, MAX_PATH, NULL) &&
		WriteProcessMemory(s_target, &s_temp_loader_section->request_event_name, s_request_event_name, MAX_PATH, NULL) &&
		WriteProcessMemory(s_target, &s_temp_loader_section->reply_event_name, s_reply_event_name, MAX_PATH, NULL)) {
		;
	}
	else {
		errln_ex("failed to load basic information into temp section");
		goto ERROR_EXIT;
	}

	/* 同步事件 */
	s_request_event = CreateEventA(NULL, false, false, s_request_event_name);
	s_reply_event = CreateEventA(NULL, false, false, s_reply_event_name);
	if (!s_request_event || !s_reply_event) {
		errln_ex("failed to create event..");
		goto ERROR_EXIT;
	}

	/* 工作线程创建 */
	s_worker =
		CreateRemoteThread(		// -> 使程序会等待远端进程的结束。
			s_target,
			NULL,
			0,
			s_r_worker_entry_point,
			s_temp_loader_section,
			0,
			NULL);
	if (!s_worker) {
		errln_ex("failed to create worker thread");
		goto ERROR_EXIT;
	}

	/* 工作线程等待 */
	if (WaitForSingleObject(s_reply_event, 5000) != WAIT_OBJECT_0) {
		errln_ex("worker thread is delaying.. Maybe something is wrong?");
		goto ERROR_EXIT;
	}
	
	/* 清理临时区段 */
	VirtualFreeEx(s_target, s_temp_loader_section, 0, MEM_RELEASE);

	/* 模块更新 */
	base_address = (PCHAR)s_my_shared_memory;
	g_manager = 
		new TrapManager(
			base_address + s_my_shared_memory->main.address_page_rva,
			base_address + s_my_shared_memory->main.flag_page_rva,
			s_request_event,
			s_reply_event,
			s_my_shared_memory->config.is_wow64
		);

	g_counter = new FastCounter(base_address + s_my_shared_memory->main.count_page_rva);

	if (s_my_shared_memory->config.is_logger_enabled) {
		g_reader =
			new LogReader(
				base_address + s_my_shared_memory->main.log_page_rva,
				base_address + s_my_shared_memory->main.flag_page_rva,
				s_my_shared_memory->logger,
				s_my_shared_memory->config.is_wow64
			);
	}

	return true;

ERROR_EXIT:
	return false;
}

void uninit_my_trap_and_watch() 
{
	uninit_my_core();
	uninit_my_shared_memory();
	unset_my_target();
}

/* 导出函数 */
bool init_my_trap_and_watch(DWORD ProcId, MyConfig& Config)
{
	/* 路径、名称的初始化 */
	if (s_shared_file_name[0] == '\0') {
		GetCurrentDirectoryW(MAX_PATH, s_my_dll_path_32);
		wcscat_s(s_my_dll_path_32, MAX_PATH, L"\\");
		wcscat_s(s_my_dll_path_32, MAX_PATH, MY_DLL_NAME_32);

		GetCurrentDirectoryW(MAX_PATH, s_my_dll_path_64);
		wcscat_s(s_my_dll_path_64, MAX_PATH, L"\\");
		wcscat_s(s_my_dll_path_64, MAX_PATH, MY_DLL_NAME_64);
		
		//// DLL测试专用路径
		//wcscpy_s(s_my_dll_path_32, MAX_PATH, TEST_DLL_PATH_32);
		//wcscat_s(s_my_dll_path_32, MAX_PATH, MY_DLL_NAME_32);

		//wcscpy_s(s_my_dll_path_64, MAX_PATH, TEST_DLL_PATH_64);
		//wcscat_s(s_my_dll_path_64, MAX_PATH, MY_DLL_NAME_64);

		_itoa_s(GetCurrentProcessId(), s_shared_file_name, MAX_PATH,10);
		strcat_s(s_shared_file_name, MAX_PATH, MappedFileName);

		_itoa_s(GetCurrentProcessId(), s_request_event_name, MAX_PATH, 10);
		strcat_s(s_request_event_name, MAX_PATH, WorkerRequestEvent);

		_itoa_s(GetCurrentProcessId(), s_reply_event_name, MAX_PATH, 10);
		strcat_s(s_reply_event_name, MAX_PATH, WorkerReplyEvent);
	}

	/* 对目标进程进行处理 */
	if (!set_my_target(ProcId)) {
		unset_my_target();
		goto ERROR_EXIT;
	}

	Config.is_wow64 = s_is_wow_64;

	/* 共享内存 */
	if (!init_my_shared_memory(Config)) {
		uninit_my_shared_memory();
		goto ERROR_EXIT;
	}

	/* 核心组件 */
	if (!init_my_core(Config)) {
		uninit_my_core();
		goto ERROR_EXIT;
	}

	return true;

ERROR_EXIT:
	return false;
}

// 进程快照获取

typedef struct
{
	DWORD proc_id;
	HWND found_hwnd;
}FindWnd;

static BOOL WINAPI _find_wnd_callback(HWND h_wnd, LPARAM lp_find_wnd)
{
	FindWnd* p_find_wnd = (FindWnd*)lp_find_wnd;

	DWORD wnd_proc_id;
	GetWindowThreadProcessId(h_wnd, &wnd_proc_id);

	if (wnd_proc_id != p_find_wnd->proc_id)
		return TRUE;

	/* 判断方式：没有父窗口，同时可见 */
	if (GetWindow(h_wnd, GW_OWNER) == NULL &&
		IsWindowVisible(h_wnd) == TRUE) {
		p_find_wnd->found_hwnd = h_wnd;
		return FALSE;
	}

	return TRUE;
}

/* 造轮子：设备路径转DOS路径 */
bool _device_path_to_dos_path(const wchar_t* device_path, wchar_t* dos_path)
{
	static wchar_t logical_drive_strings[MAX_PATH] = { 0 };
	static wchar_t current_device_name[MAX_PATH] = { 0 };

	size_t current_drive_string_size = 0;
	wchar_t* p_current_drive_string = NULL;

	dos_path[0] = L'\0';

	/* 获取所有可能的盘符 */
	if (logical_drive_strings[0] == L'\0') {

		if (!GetLogicalDriveStringsW(MAX_PATH, logical_drive_strings)) {
			errln_ex("failed to get logical drive strings..?");
			return false;
		}

		/* 截断所有的尾符 */
		DWORD index = 0;
		while (index < MAX_PATH)
		{
			if (logical_drive_strings[index] == L'\\')
				logical_drive_strings[index] = L'\0';

			index++;
		}
	}

	/* 依次进行匹配 */
	p_current_drive_string = logical_drive_strings;
	while (p_current_drive_string - logical_drive_strings < MAX_PATH) {

		if (*p_current_drive_string == L'\0') {
			p_current_drive_string++;
			continue;
		}

		current_drive_string_size = wcslen(p_current_drive_string);

		QueryDosDeviceW(p_current_drive_string, current_device_name, MAX_PATH);

		/* 匹配成功 */
		if (wcsstr(device_path, current_device_name) == device_path) {

			//盘符，例如 "C:"
			wcscpy_s(dos_path, MAX_PATH, p_current_drive_string);
			//后路径，例如 "\windows\system32\user32.dll"
			size_t device_name_length = wcslen(current_device_name);
			wcscat_s(dos_path, MAX_PATH - device_name_length, device_path + device_name_length);

			return true;
		}

		p_current_drive_string += current_drive_string_size;
	}

	return false;
}

void get_process_snapshot(std::vector<MyProcSnapshot>& proc_list)
{
	/* 用于临时存放设备路径 */
	static wchar_t temp_device_path[MAX_PATH] = { 0 };

	/* 释放原有资源 */
	auto ite = proc_list.begin();
	while (ite != proc_list.end()) {
		DestroyIcon(ite->icon);
		ite++;
	}
	proc_list.clear();

	/* 开始枚举 */
	HANDLE h_proc_snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (!h_proc_snap) {
		errln_ex("failed to build a process snapshot");
		return;
	}

	PROCESSENTRY32 pe32;
	pe32.dwSize = sizeof(PROCESSENTRY32);
	if (!Process32First(h_proc_snap, &pe32)) {
		errln_ex("failed to get first process snapshot");
		return;
	}

	HANDLE h_query_proc = NULL;
	MyProcSnapshot snapshot;
	do {

		ZeroMemory(&snapshot, sizeof(MyProcSnapshot));

		if (pe32.th32ProcessID == GetCurrentProcessId())
			continue;
		if (pe32.th32ProcessID == 0 ||
			pe32.th32ProcessID == 4)
			continue;

		h_query_proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, false, pe32.th32ProcessID);
		if (h_query_proc == NULL)
			continue;

		/* PID */
		snapshot.pid = pe32.th32ProcessID;

		/* 路径获取 */
		GetProcessImageFileNameW(h_query_proc, temp_device_path, MAX_PATH);
		if (!_device_path_to_dos_path(temp_device_path, snapshot.file_path)) {
			wprintf(L"[ERR] failed to find convert to dos path.. device path = %s", temp_device_path);
		}

		/* 截取文件名 */
		wchar_t* p_name = wcsrchr(snapshot.file_path, L'\\');
		if (p_name == NULL)
			p_name = wcsrchr(snapshot.file_path, L'/');
		if (p_name == NULL)
			p_name = snapshot.file_path;
		else
			p_name++;
		snapshot.file_name_rva = p_name - snapshot.file_path;

		/* 窗口句柄 + 窗口名 */
		FindWnd my_fnd;
		my_fnd.found_hwnd = NULL;
		my_fnd.proc_id = pe32.th32ProcessID;
		EnumWindows(_find_wnd_callback, (LPARAM)&my_fnd);
		if (my_fnd.found_hwnd) {
			snapshot.main_wnd = my_fnd.found_hwnd;
			GetWindowTextW(my_fnd.found_hwnd, snapshot.wnd_text, MAX_PATH);
		}

		/* 图标 */
		SHFILEINFOW sh_info;
		ZeroMemory(&sh_info, sizeof(SHFILEINFOW));
		if (!SHGetFileInfoW(snapshot.file_path, 0, &sh_info, sizeof(sh_info), SHGFI_ICON)) {
			errln_ex("failed to get file icon");
		}
		snapshot.icon = sh_info.hIcon;

		proc_list.push_back(snapshot);

	} while (Process32Next(h_proc_snap, &pe32));


}