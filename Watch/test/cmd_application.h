#pragma once

#include <map>
#include "../include/client.h"

/*
 *
 *	文件作用：提供用户接口
 * 
 */

//
// 基本应用层支持
//

/* 文件解析 */
void init_task_loading(ModuleParser* Parser);
bool load_tasks_from_file(const char* lpFileName, std::map<PVOID, std::string>* lpTaskBook);

/* 基本命令 */
void print_state();
void print_proc_list(bool OnlyWnd);
void print_module_list(const char* lpModuleName);
void print_set_list(const char* lpSetName);
void print_hot_tasks();
void attach_to_target(DWORD ProcId, bool EnableLogging);
void detach_from_target();
void commit_task_set(const char* lpSetName);
void decommit_task_set(const char* lpSetName);

/* 计数器命令 */
void print_counter_stats();
void set_counter_pick_type(const char* lpPickType);
void set_counter_sort_type(const char* lpSortType);

/* 日志记录器命令 */
void print_tracing_list();
void enable_tracing(std::vector<DWORD>& IdArray);
void disable_tracing(std::vector<DWORD>& lpIdArray);
void print_raw_log_data(DWORD Rva);
void print_log_data();
void set_log_reader(bool EnableOrNot);
void print_hash_record(DWORD hash);
