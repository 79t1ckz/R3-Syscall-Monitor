#pragma once

#include "../src/trap_manager.h"
#include "../src/fast_counter.h"
#include "../src/log_reader.h"

/* 模块解析模块 */
#include "C:/Users/86158/source/repos/My_Libs/Module_Parse/include/parser.h"

#ifdef NDEBUG
	#ifdef _WIN64
		#pragma comment(lib, "C:\\Users\\86158\\source\\repos\\My_Libs\\Module_Parse\\x64\\Release\\Module_Parse.lib")
	#else
		#pragma comment(lib, "C:\\Users\\86158\\source\\repos\\My_Libs\\Module_Parse\\Release\\Module_Parse.lib")
	#endif
#else
	#ifdef _WIN64
		#pragma comment(lib, "C:\\Users\\86158\\source\\repos\\My_Libs\\Module_Parse\\x64\\Debug\\Module_Parse.lib")
	#else
		#pragma comment(lib, "C:\\Users\\86158\\source\\repos\\My_Libs\\Module_Parse\\Debug\\Module_Parse.lib")
	#endif
#endif

/*
	
	客户端唯一头文件。

	操作与执行函数逻辑：
	1. 打开一个进程 -> 初始化内存区段、模块
	2. 关闭一个进程 -> 删除内存区段、模块
	3. 进行监视操作 -> 直接操作类对象

	需要隐藏的信息：
	1. 共享内存的句柄、地址
	2. 同步事件的两个句柄（需要先创建陷阱管理器，在创建工作线程）

*/

/* 日志记录 */
#define infoln(format, ...) printf(format "\n", ##__VA_ARGS__)
#define errln(format, ...) printf("[ERR]" format "\n", ##__VA_ARGS__)
#define errln_ex(format, ...) printf("[ERR %d]" format "\n", GetLastError(), ##__VA_ARGS__)
#define bugln(format, ...) printf("[***BUG***]" format "\n", ##__VA_ARGS__)

/* 进程快照信息 */
typedef struct
{
	DWORD pid;
	DWORD file_name_rva;
	HICON icon;
	HWND main_wnd;		// --> 区分应用和系统进程的主要方式
	wchar_t file_path[MAX_PATH];	// UTF-16
	wchar_t wnd_text[MAX_PATH];	// UTF-16
}MyProcSnapshot;

/* 全局对象 */
extern ModuleParser* g_parser;
extern TrapManager* g_manager;
extern FastCounter* g_counter;
extern LogReader* g_reader;

/* 导出函数 */
bool _device_path_to_dos_path(const wchar_t* device_path, wchar_t* dos_path);
void get_process_snapshot(std::vector<MyProcSnapshot>& proc_list);
bool init_my_trap_and_watch(DWORD ProcId, MyConfig& Config);
void uninit_my_trap_and_watch();