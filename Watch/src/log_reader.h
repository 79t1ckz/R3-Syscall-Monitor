#pragma once

#include <windows.h>
#include <vector>
#include <map>
#include "../../shared/r_structs.h"

/* 
	由外部实现的操作：
	1. 停止/回复所有陷阱项的日志记录：保存现有启用项，全部启用/禁用
*/

/* 日志数据 */
typedef struct {
	WORD trap_id;
	DWORD thread_id;
	DWORD call_hash;
}LogData;

typedef struct {

	DWORD ref;
	std::vector<PVOID> list;
}CallChainInfo;

class LogReader
{
private:

	/* 压缩过的日志数据 */
	typedef struct {
		WORD trap_id;		// 必选
		WORD thread_index;	// 没有填 -1
		DWORD call_hash;	// 没有填 0
	}LogDataC;

	/* 压缩表的索引 */
	typedef struct {
		DWORD head;		// 指向首个有效项
		DWORD tail;		// 指向首个无效项
		DWORD ref;		// 引用次数
	}Priv_CallChainInfo;

	/* 读取 */
	bool is_wow64;
	DWORD shadow_head;
	PCHAR log_page;
	BYTE* flag_page;
	LoggerBrain* logger;

	/* 线程操纵 */
	bool dead_request;
	bool pause_request;
	bool flush_request;
	bool skip_logging_request;
	HANDLE h_reader;

	/* 同步 */
	CRITICAL_SECTION log_lock;
	CRITICAL_SECTION call_chain_lock;
	CRITICAL_SECTION thread_index_lock;

	/* 数据管理（访问操作均需要同步） */
	std::map<DWORD, Priv_CallChainInfo> hash_to_call_chain;
	std::vector<PVOID> compressed_call_chains;
	std::vector<LogDataC> compressed_log_records;
	std::vector<DWORD> thread_id_to_index;

public:
	LogReader(PVOID pLogPage, PVOID pFlagPage, LoggerBrain& Brain, bool IsWow64);
	~LogReader();

	/* 特殊线程相关 */
	void clear();
	bool start(bool NeedFlushCache = true);
	void stop(bool NeedSkipLogginng = true);
	bool query_hash(DWORD hash_value, CallChainInfo& recv_list);
	unsigned int dupe_log_record(std::vector<LogData>& recv_list, DWORD start_rva, DWORD dupe_count);

	/* 启用/禁用追踪 */
	void set_trace(WORD TrapId, bool Enable);
	bool is_tracing(WORD TrapId);

	/* 查询函数 */
	unsigned int log_count();

	/* 测试专用 */
	void print_log_data_info();

	friend DWORD WINAPI _reader_thread_func(LogReader* lpThis);

private:

	/* 操作LoggerBrain */
	PCHAR _read();
	void _read_done();

	unsigned int _get_log_block_size(LogHead& Head);
	void _record_log_block(PCHAR lpLogBlock);
	void _record_hash(LogHead& head, DWORD hash, PVOID lpCallChain);
};

DWORD WINAPI _reader_thread_func(LogReader* lpThis);