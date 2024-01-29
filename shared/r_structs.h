#pragma once
#include <windows.h>

/* 

	共享数据结构。

	通信方式：映射大区域共享内存。
	由客户端决定共享内存的大小与启用功能

	--------
	控制区 - 0x1000 (1 page)
		MainBrain
		LogBrain
	--------
	描述符区 - 0x1000 (1 page)
		客户端单线程写入
	--------
	地址区 - 0x4000 / 0x8000 (8 pages)
		服务端单线程写入
	--------
	计数区 - 0x4000 (4 pages)
		服务端多线程写入（低同步要求）
	--------
	追踪日志记录区 - 0x100000 (100 pages)
		服务端多线程写入（较高同步要求）
	--------
	No-Access区，用于防止越界的出现。

*/

#define MaxSlotCount		0x1000
#define LogPageSize			0x10000
#define MappedFileName		"_shared_memory_for_WTL_module"
#define WorkerRequestEvent	"_worker_thread_request_event"
#define WorkerReplyEvent	"_worker_thread_reply_event"

/* 陷阱标识符 */
#define TF_Using			0x1
#define TF_NeedTrace		0x2
#define TF_ChangeRequest	0x4
#define TF_Error			0x8

enum class MyWorkerState
{
	Busy,
	Free
};

/*
	临时区段信息
*/
typedef struct
{
	DWORD client_id;
	char shared_file_name[MAX_PATH];
	char request_event_name[MAX_PATH];
	char reply_event_name[MAX_PATH];
}MyTempSection;

/*
	共享区段配置信息
*/
typedef struct
{
	bool is_wow64;			// 是否x64 -> x86？
	bool is_logger_enabled;	// 决定LoggerBrain是否有效

}MyConfig;

/*
	最基本的功能模块
*/
typedef struct
{
	bool close_worker_request;	// 用于指示工作线程退出

	DWORD flag_page_rva;
	DWORD address_page_rva;
	DWORD count_page_rva;
	DWORD log_page_rva;			// 可以为0，代表不使用日志记录

}MainBrain;

/*
	日志记录专用模块
*/
typedef struct
{
	int capability;
	int head;
	int tail;
	DWORD dropped;
	DWORD sent;

	/* 特殊标志 */
	bool clear_cache_flag;
	bool skip_log_flag;

}LoggerBrain;

typedef struct
{
	MyConfig config;	// --> 用于判断类型
	MainBrain main;
	LoggerBrain logger;
}MySharedFileHead;

/*
	 日志描述符（共16个槽位）
	（全0即为无效）
	[标识符] ... [数值]
*/
#define LH_Mask_CallChainLength	(0x40 - 1)	// 6-bit，表示尾部的函数调用链的长度
#define LH_Mask_Omitted			0x2000		// 1-bit，代表是否省略了函数调用链（代表有哈希但是无调用链）
#define LH_Mask_ThreadId		0x4000		// 1-bit，线程ID
#define LH_Mask_Valid			0x8000		// 1-bit，用于区分出无效项

typedef struct
{
	WORD descriptor;
	WORD trap_id;
}LogHead;