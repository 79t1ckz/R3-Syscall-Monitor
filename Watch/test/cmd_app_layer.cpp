#include "../include/client.h"
#include "cmd_application.h"

#include <format>

/*
 *
 *	文件作用：提供原始的应用层
 *
 */

//
// 任务集管理
//
typedef std::map<PVOID, std::string> TaskSet;
static TaskSet* s_last_query_set;
static std::string s_last_query_set_name;
static std::map<std::string, TaskSet*> s_task_set_book;

//
// 状态显示
//
static MyConfig s_config;
static int s_cur_snapshot_index = -1;
static std::vector<MyProcSnapshot> s_snapshot_list;

//
// 计数器管理
//

//
// 日志读取器管理
//
static bool s_is_reader_running = false;
static int s_tracing_count = 0;

void print_state()
{
	int i;

	/* 基本信息 */
	printf("<basic information>\n");
	if (s_cur_snapshot_index == -1) {
		printf("\tno target\n");
	}
	else {
		printf("\tPid: %d\n", s_snapshot_list[s_cur_snapshot_index].pid);
		wprintf(L"\tName: %s\n", s_snapshot_list[s_cur_snapshot_index].file_path
			+ s_snapshot_list[s_cur_snapshot_index].file_name_rva);
	}
	//printf("\n");

	/* 钩子处理 */
	if (g_manager) {
		DWORD hot_count = g_manager->committed_count();
		printf("<trap manager>\n");
		printf("\tcapability: ( %d / 4096 ) - %.2lf%%\n", hot_count, hot_count * 100.0 / MaxSlotCount);
		printf("\tcommitted set:\n");
		i = 0;
		auto set_ite = s_task_set_book.begin();
		while (set_ite != s_task_set_book.end()) {

			if (i % 3 == 0) printf("\t");

			printf("%16s - %-4d", set_ite->first.c_str(), (DWORD)set_ite->second->size());

			if (i && (i + 1) % 3 == 0) printf("\n");
			else printf(", ");

			i++;
			set_ite++;
		}
		if (s_task_set_book.empty()) printf("\t( nothing )\n");
		if (i % 3) printf("\n");
	}

	/* 快速计数信息 */
	if (g_counter) {
		printf("<fast counter>\n");
		printf("\tPick type: ");
		switch (g_counter->pick_type) {
		case PickType::So_Far:
			printf("count so far\n");
			break;
		case PickType::Recently:
			printf("count recently\n");
			break;
		case PickType::Recently_Freq:
			printf("frequent recently\n");
			break;
		}
		printf("\tSort type: ");
		switch (g_counter->sort_type) {
		case SortType::By_Id:
			printf("by id\n");
			break;
		case SortType::Value_Top_Down:
			printf("by value top down\n");
			break;
		case SortType::Freq_Close_To_60x:
			printf("by close to 60x (only frequency)\n");
			break;
		case SortType::Freq_Close_To_10x:
			printf("by close to 10x (only frequency)\n");
			break;
		}
	}

	/* 日志信息 */
	if (g_reader) {
		printf("<Logger>\n");
		printf("\tLog Buffer: %d / 0x8000 ( %.2lf%% )\n", g_reader->log_count(), g_reader->log_count() * 100.0 / 0x8000);
		printf("\tTracing Count: %d\n", s_tracing_count);
		printf("\tLog reader state: ");
		if (s_is_reader_running) printf("running  ( o _ o )!\n");
		else printf("sleeping  ( - . - )zZ\n");
	}

	printf("<end>\n");
}

/* 加载任务集 */
TaskSet* load_task_set(const char* lpTaskSetName, bool LoadFromFileIfNotFound)
{
	TaskSet* p_set = new TaskSet;
	std::map<std::string, TaskSet*>::iterator iter;
	if ((iter = s_task_set_book.find(lpTaskSetName)) != 
		s_task_set_book.end()) {
		// printf("<warn> %s is already loaded!\n", lpTaskSetName);
		return iter->second;
	}
	else if (LoadFromFileIfNotFound == false) {
		return NULL;
	}

	if (load_tasks_from_file(lpTaskSetName, p_set)) {
		std::string set_name = lpTaskSetName;
		s_task_set_book.emplace(set_name, p_set);
	}
	else {
		delete p_set;
		p_set = NULL;
	}

	return p_set;
}

/* 删除任务集 */
bool delete_task_set(const char* lpTaskSetName)
{
	bool is_deleted = false;
	std::map<std::string, TaskSet*>::iterator iter;

	/* 清理指定项 */
	if (lpTaskSetName &&
		(iter = s_task_set_book.find(lpTaskSetName)) != s_task_set_book.end()) {
		if (iter->second == s_last_query_set) s_last_query_set = NULL;
		delete iter->second;
		s_task_set_book.erase(iter);
		is_deleted = true;
	}
	/* 清理全部 */
	else if (lpTaskSetName == NULL) {
		iter = s_task_set_book.begin();
		while (iter != s_task_set_book.end()) {
			delete iter->second;
			iter++;
		}
		s_task_set_book.clear();
		is_deleted = true;
		s_last_query_set = NULL;
	}

	return is_deleted;
}

/* 搜索任务项 */
bool query_task_entry(PVOID TaskAddr, std::string* TaskNameSlot, std::string* TaskStringSlot) 
{
	TaskSet::iterator task_iter;
	std::map<std::string, TaskSet*>::iterator set_iter;

	/* 优先搜索上一个点 */
	if (s_last_query_set &&
		(task_iter = s_last_query_set->find(TaskAddr)) != s_last_query_set->end()) {
		goto FOUND_IT;
	}

	/* 全面搜索 */
	set_iter = s_task_set_book.begin();
	while (set_iter != s_task_set_book.end()) {
		task_iter = set_iter->second->find(TaskAddr);
		if (task_iter != set_iter->second->end()) {
			s_last_query_set = set_iter->second;
			s_last_query_set_name = set_iter->first;
			goto FOUND_IT;
		}
		set_iter++;
	}

	if (TaskNameSlot) *TaskNameSlot = "?";
	// if (TaskStringSlot) *TaskStringSlot = "?";
	return false;

FOUND_IT:
	if (TaskNameSlot) *TaskNameSlot = task_iter->second;
	// if (TaskStringSlot) *TaskStringSlot = task_iter->second;
	return true;
}

/* 进程快照打印 */
void print_proc_list(bool OnlyWnd)
{
	if (s_cur_snapshot_index != -1) {
		printf("now we are still having a watch target!\n");
		return;
	}

	get_process_snapshot(s_snapshot_list);
	auto ite_proc = s_snapshot_list.begin();
	while (ite_proc != s_snapshot_list.end()) {
		if (OnlyWnd && ite_proc->main_wnd == NULL) {
			ite_proc++;
			continue;
		}
		printf("->\tPid: %d\n", ite_proc->pid);
		wprintf(L"\tName: %s\n", ite_proc->file_path + ite_proc->file_name_rva);
		wprintf(L"\tPath: %s\n", ite_proc->file_path);
		if (ite_proc->main_wnd)
			wprintf(L"\tWndText: %s\n", ite_proc->wnd_text);
		printf("\n");
		ite_proc++;
	}
}

/* 进程模块打印输出 */
void print_module_list(const char* lpModuleName = NULL)
{
	static wchar_t temp_file_path[MAX_PATH] = { 0 };

	g_parser->walkAddressSpace();

	/* 要求打印具体的模块，打印所有已知的导出函数 */
	if (lpModuleName) {
		ModuleInfo* p_module = g_parser->queryModule(lpModuleName);
		std::map<PVOID, std::string> export_book;
		EdtIter iter_name(p_module, EdtIter::By_Name);
		while (g_parser->ite(&iter_name)) {
			export_book.emplace(iter_name.addr_value, iter_name.name);
		}
		EdtIter iter_oridinal(p_module, EdtIter::By_Oridinal);
		std::string oridinal_name;
		while (g_parser->ite(&iter_oridinal)) {
			if (export_book.find(iter_oridinal.addr_value) == export_book.end())
				continue;
			oridinal_name = std::format("#{0:X}", iter_oridinal.oridinal);
			export_book.emplace(iter_oridinal.addr_value, oridinal_name);
		}
		printf("<%s>\n", lpModuleName);
		auto ite_book = export_book.begin();
		while (ite_book != export_book.end()) {
			printf("\t%s\n", ite_book->second.c_str());
			ite_book++;
		}
		printf("</%s>\n", lpModuleName);
	}
	/* 打印所有可能的模块，要求信息：名称、地址、完整路径 */
	else {
		printf("Modules in the process:\n");
		auto ite_module = g_parser->ModuleInfoBook.begin();
		while (ite_module != g_parser->ModuleInfoBook.end()) {
			wprintf(L"\tname = %s\n", ite_module->second.name.c_str());
			printf("\taddress %p\n", ite_module->first);
			if (_device_path_to_dos_path(ite_module->second.path.c_str(), temp_file_path)) {
				wprintf(L"\tpath = %s\n", temp_file_path);
			}
			else {
				wprintf(L"\tdevice path = %s\n", ite_module->second.path.c_str());
			}
			printf("\n");
			ite_module++;
		}
		
	}
}

/* 打印已存在的任务集信息 */
void print_set_list(const char* lpSetName)
{
	/* 特定任务集 */
	if (lpSetName) {
		TaskSet* p_set = load_task_set(lpSetName, false);
		if (p_set == NULL) {
			printf("can't find a task set named: %s\n", lpSetName);
		}
		else {
			printf("task set: %s\n", lpSetName);
			auto task_iter = p_set->begin();
			while (task_iter != p_set->end()) {
				printf("\t%p\t%s\n", task_iter->first, task_iter->second.c_str());
				task_iter++;
			}
		}
	}
	/* 所有任务集（单名称） */
	else {
		printf("task set list:\n");
		auto set_iter = s_task_set_book.begin();
		while (set_iter != s_task_set_book.end()) {
			printf("\t%s\n", set_iter->first.c_str());
			set_iter++;
		}
	}
	printf("\n");
}

/* 打印已提交的任务 */
void print_hot_tasks()
{
	PVOID trap_addr;
	std::string trap_string;
	std::string trap_source;
	printf("here are hot tasks:\n");
	for (int i = 0; i < 0x1000; i++) {
		trap_addr = g_manager->query_address(i);
		if (trap_addr == NULL) continue;
		query_task_entry(trap_addr, &trap_string, &trap_source);
		printf("\t%4d\t%p\t%20s\n", i, trap_addr, trap_string.c_str());
	}
	printf("\n");
}

/* 进程附加 */
void attach_to_target(DWORD ProcId, bool EnableLogging)
{
	bool is_found = false;
	for (int i = 0; i < s_snapshot_list.size(); i++) {
		if (s_snapshot_list[i].pid == ProcId) {
			is_found = true;
			s_cur_snapshot_index = i;
			break;
		}
	}

	if (is_found == false) {
		printf("<err> failed to find pid = %d\n", ProcId);
		return;
	}

	s_config.is_logger_enabled = EnableLogging;
	if (init_my_trap_and_watch(ProcId, s_config) == false) {
		s_cur_snapshot_index = -1;
	}

	s_is_reader_running = false;
	s_tracing_count = 0;
}

/* 进程脱离 */
void detach_from_target()
{
	uninit_my_trap_and_watch();
	s_cur_snapshot_index = -1;
}

/* 提交/收回的异常处理，返回值代表是否再次尝试 */
static bool task_error_handler(TrapManager::TrapResult Result, bool IsSecondChance)
{
	if (Result == TrapManager::No_Error) {
		return false;
	}

	switch (Result) {
	case TrapManager::Time_Out:
		printf("<TrapError> Trap handler time out. Maybe the process is dead.\n");
		break;
	case TrapManager::Not_Enough_Free_Slot:
		printf("<TrapError> Free trap slot is ran out. Try to decommit some tasks.\n");
		break;
	case TrapManager::Detour_Failure:
		printf("<TrapError> Detour Failed. Maybe reached a bad address, or a breakpoint?\n");
		break;
	}

	/* 拦截失败，可以给出第二次机会 */
	if (Result != TrapManager::Detour_Failure)
		return false;

	std::string trap_string;
	printf("Here are failed tasks:\n");
	for (int i = 0; i < g_manager->tasks_failed.size(); i++) {
		query_task_entry(g_manager->tasks_failed[i], &trap_string, NULL);
		printf("\t%p\t%s\n", g_manager->tasks_failed[i], trap_string.c_str());
	}

	if (IsSecondChance) {
		printf("<Trap> Stopped at the second chance.\n");
		return false;
	}

	char ch;
	printf("Do you want to ignore them and try again? (Y/n)\n");
	ch = getchar();
	rewind(stdin);
	return ch == 'Y';
}

void commit_task_set(const char* lpSetName)
{
	bool is_reload;
	TaskSet* p_set;
	is_reload = delete_task_set(lpSetName);
	p_set = load_task_set(lpSetName, true);
	if (p_set == NULL) return;
	auto ite = p_set->begin();
	while (ite != p_set->end()) {
		g_manager->tasks_pending.push_back(ite->first);
		ite++;
	}

	bool is_second_chance = false;
	while (task_error_handler(g_manager->commit(), is_second_chance)) {
		is_second_chance = true;
	}

	if (is_reload) {
		printf("<Trap> Task set %s is reloaded.\n", lpSetName);
	}
	else {
		printf("<Trap> Task set %s is loaded.\n", lpSetName);
	}
}

void decommit_task_set(const char* lpSetName) 
{
	/* 生成任务集 */
	if (lpSetName) {
		TaskSet* p_set = load_task_set(lpSetName, false);
		if (p_set == NULL) return;
		auto ite = p_set->begin();
		while (ite != p_set->end()) {
			g_manager->tasks_pending.push_back(ite->first);
			ite++;
		}
		delete_task_set(lpSetName);
	}
	/* 清理全部 */
	else {
		delete_task_set(NULL);
	}

	bool is_second_chance = false;
	while (task_error_handler(
		g_manager->decommit(lpSetName == NULL),
		is_second_chance)) {
		is_second_chance = true;
	}
}

// \t<trap id> - <stats> - <name>
// \t%9d   %7d   %s
void print_counter_stats()
{
	int i;
	PVOID trap_addr;
	std::string trap_name;
	g_counter->update();
	g_counter->sort();
	system("cls");
	/* 无捕获数据 */
	if (g_counter->counter_report.empty()) {
		printf("<counter> no caught stats\n");
		return;
	}
	/* 有捕获数据 */
	printf("<counter> here are stats:\n");
	printf("\t<trap id> - <statistics> - <name>\n");
	printf("-----------------------------------------------\n");
	for (i = 0; i < g_counter->counter_report.size(); i++) {
		printf("\t %7d ", g_counter->counter_report[i].trap_id);
		if (g_counter->pick_type == PickType::So_Far)
			printf("    %10d ", g_counter->counter_report[i].u.count_all_time);
		else if (g_counter->pick_type == PickType::Recently)
			printf("    %10d ", g_counter->counter_report[i].u.count);
		else
			printf("    %10.2lf ", g_counter->counter_report[i].u.freq);
		trap_addr = g_manager->query_address(g_counter->counter_report[i].trap_id);
		query_task_entry(trap_addr, &trap_name, NULL);
		printf("    %s\n", trap_name.c_str());
	}
	printf("\n");
}

void set_counter_pick_type(const char* lpPickType)
{
	if (!strcmp(lpPickType, "cnt")) {
		g_counter->pick_type = PickType::Recently;
	}
	else if (!strcmp(lpPickType, "frq")) {
		g_counter->pick_type = PickType::Recently_Freq;
	}
	else if (!strcmp(lpPickType, "cnt_all")) {
		g_counter->pick_type = PickType::So_Far;
	}
	else {
		throw std::exception("unsupported argument");
	}
}

void set_counter_sort_type(const char* lpSortType)
{
	if (!strcmp(lpSortType, "id")) {
		g_counter->sort_type = SortType::By_Id;
	}
	else if (!strcmp(lpSortType, "value")) {
		g_counter->sort_type = SortType::Value_Top_Down;
	}
	else if (!strcmp(lpSortType, "10x")) {
		g_counter->sort_type = SortType::Freq_Close_To_10x;
	}
	else if (!strcmp(lpSortType, "60x")) {
		g_counter->sort_type = SortType::Freq_Close_To_60x;
	}
	else {
		throw std::exception("unsupported argument");
	}
}

/* 仅打印追踪项 */
void print_tracing_list()
{
	int i;
	PVOID trap_addr;
	std::string trap_name;
	printf("tracing list:\n");
	for (i = 0; i < MaxSlotCount; i++) {
		trap_addr = g_manager->query_address(i);
		if (!trap_addr) continue;
		if (!g_reader->is_tracing(i)) continue;
		query_task_entry(trap_addr, &trap_name, NULL);
		printf("\t%d\t%p\t%s\n", i, trap_addr, trap_name.c_str());
	}
	printf("\n");
}

void enable_tracing(std::vector<DWORD>& IdArray)
{
	if (IdArray.empty()) {
		printf("<Trace> no new trace enabled.\n");
		return;
	}

	int i;
	for (i = 0; i < IdArray.size(); i++)
	{
		if (g_manager->query_address(IdArray[i]) == NULL) {
			printf("<warn> trap id = %d is invalid", IdArray[i]);
			continue;
		}
		g_reader->set_trace(IdArray[i], true);
		s_tracing_count++;
	}
}

void disable_tracing(std::vector<DWORD>& IdArray)
{
	int i;
	/* 清除全部 */
	if (IdArray.empty()) {
		for (i = 0; i < MaxSlotCount; i++) {
			g_reader->set_trace(i, false);
		}
		s_tracing_count = 0;
	}
	/* 仅清除部分 */ 
	else{
		for (i = 0; i < IdArray.size(); i++) {
			if (g_manager->query_address(IdArray[i]) == NULL) {
				printf("<warn> trap id = %d is invalid", IdArray[i]);
				continue;
			}
			g_reader->set_trace(IdArray[i], false);
			s_tracing_count--;
		}
	}
}

void print_raw_log_data(DWORD Rva)
{
	int i;
	std::vector<LogData> raw_data_list;
	g_reader->dupe_log_record(raw_data_list, Rva, 100);
	if (raw_data_list.empty()) {
		printf("<Logger> no record starts at %d\n", Rva);
	}

	/* 存在有效项 */
	system("cls");
	printf("<Logger> log record [%d, %d) from %d\n",
		Rva,
		Rva + raw_data_list.size(),
		g_reader->log_count());
	
	/* 打印项 */
	PVOID trap_addr;
	std::string trap_name;
	printf("\t<index> - <trap id> - <thread id> - <call hash> - <trap name>\n");
	printf("\t---------------------------------------------------------------\n");
	for (i = 0; i < raw_data_list.size(); i++) {
		trap_addr = g_manager->query_address(raw_data_list[i].trap_id);
		query_task_entry(trap_addr, &trap_name, NULL);
		printf("\t%5d  -  %7d  -  %9d  -   %08X  -  %s\n",
			Rva + i,
			raw_data_list[i].trap_id,
			raw_data_list[i].thread_id,
			raw_data_list[i].call_hash,
			trap_name.c_str()
		);
	}
	printf("\n");
}

/* 基于调用链的堆叠器 */
class LogStacker
{
private:
	WORD trap_id;
	DWORD total_count;
	std::map<DWORD, DWORD> call_stat;
public:
	std::vector<std::pair<DWORD, DWORD>> call_stat_list;
	
public:
	LogStacker(WORD trap_id) {
		this->trap_id = trap_id;
		this->total_count = 0;
	}

	void inc(DWORD hash) {
		call_stat[hash]++;
		total_count++;
	}

	DWORD count() {
		return total_count;
	}

	/* 结算 */
	void balance()
	{
		auto ite = call_stat.begin();
		while (ite != call_stat.end()) {
			call_stat_list.push_back(*ite);
			ite++;
		}

		std::sort(
			call_stat_list.begin(),
			call_stat_list.end(),
			[](std::pair<DWORD, DWORD>& first, std::pair<DWORD, DWORD>& next) -> bool
			{
				return first.second > next.second;
			});
	}
};

static std::map<WORD, LogStacker> s_log_stacker_book;

/* 堆叠数据，并打印 */
void print_log_data()
{
	int i;
	std::vector<LogData> raw_data_list;
	g_reader->dupe_log_record(raw_data_list, 0, -1);

	if (raw_data_list.empty()) {
		printf("<Logger> no caught log records..\n");
		return;
	}

	system("cls");

	/* 计数阶段 */
	for (i = 0; i < raw_data_list.size(); i++)
	{
		s_log_stacker_book.emplace(
			raw_data_list[i].trap_id,
			LogStacker(raw_data_list[i].trap_id)
		).first->second.inc(raw_data_list[i].call_hash);
	}

	/* 结算 & 计数阶段 */
	auto iter = s_log_stacker_book.begin();
	PVOID trap_addr;
	std::string trap_name;
	while (iter != s_log_stacker_book.end()) {
		trap_addr = g_manager->query_address(iter->first);
		query_task_entry(trap_addr, &trap_name, NULL);
		printf("\ttrap id: %d\tcount: %d\t%s\n", iter->first, iter->second.count(), trap_name.c_str());
		printf("\t------------------------------------------\n");
		iter->second.balance();
		for (i = 0; i < iter->second.call_stat_list.size(); i++) {
			printf("\t\t%08X\t%d\t%.2lf%%\n",
				iter->second.call_stat_list[i].first,
				iter->second.call_stat_list[i].second,
				iter->second.call_stat_list[i].second * 100.0 / iter->second.count());
		}
		printf("\n");
		iter++;
	}

	s_log_stacker_book.clear();
}

void set_log_reader(bool EnableOrNot)
{
	if (EnableOrNot) {
		if (s_is_reader_running == EnableOrNot) {
			printf("<Logger> reader is already running!\n");
			return;
		}
		else {
			g_reader->start();
			printf("<Logger> reader is running now!\n");
			s_is_reader_running = EnableOrNot;
		}
	}
	else {
		if (s_is_reader_running == EnableOrNot) {
			printf("<Logger> reader is already sleeping!\n");
			return;
		}
		else {
			g_reader->start();
			printf("<Logger> reader fall asleep now!\n");
			s_is_reader_running = EnableOrNot;
		}
	}
}

/* 打印哈希调用链 */
void print_hash_record(DWORD hash)
{
	CallChainInfo info;
	if (g_reader->query_hash(hash, info) == false) {
		printf("<Logger> can't find the call chain list\n");
	}

	/* 存在调用链 */
	ModuleInfo* p_info = NULL;
	printf("<Logger> Hash %08X is the call chain list:\n", hash);
	printf("ref count = %d (%.2lf%%)\n", info.ref, info.ref * 100.0 / g_reader->log_count());
	auto ite = info.list.begin();
	while (ite != info.list.end()) {
		
		/* 打印所属模块 */
		if (p_info != g_parser->queryModule(*ite) ||
			ite == info.list.begin()) {
			p_info = g_parser->queryModule(*ite);
			if (p_info) wprintf(L"-----------------------[%s]-----------------------\n", p_info->name.c_str());
			else printf("-----------------------[? ? ?]-----------------------\n");
		}

		printf("\t%p", *ite);
		if (p_info) {
			printf("\t+0x%X", (DWORD)((PCHAR)*ite - (PCHAR)p_info->image_base));
		}
		printf("\n");
		ite++;
	}

	printf("\n");
}