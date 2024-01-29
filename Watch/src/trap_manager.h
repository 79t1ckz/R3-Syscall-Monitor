#pragma once

#include <windows.h>
#include <vector>
#include <map>
#include "../../shared/r_structs.h"

/*

	封装捕捉器管理。

	初始化要求：
		1. 两个返还句柄。2. 标志页地址。3. 地址页地址。

	输入 -> 任务项
	输出 -> 【错误】错误项 + 排队项
			【正确】无。

	职能：
		1. 修改地址页、标志页
		2. 激活事件

	处理：
		1. 存在不能钩的项？-> 终止/忽略操作，生成表项
		2. 槽位已满？-> 终止/忽略操作

*/
class TrapManager {

public:
	enum TrapResult {
		No_Error,
		Not_Enough_Free_Slot,
		Detour_Failure,
		Time_Out
	};

public:
	std::vector<PVOID> tasks_pending;
	std::vector<PVOID> tasks_failed;

private:
	/* 必需的外部数据 */
	bool is_processing_wow64;
	HANDLE request_event;
	HANDLE reply_event;
	BYTE* flag_page;
	PVOID address_page;
	PVOID* shadow_address_page;		// 需要自行生成。

	std::vector<std::pair<PVOID, WORD>> tasks_doing;	// -> 转变中任务，索引一定是一个有效项。
	std::map<PVOID, unsigned int> address_to_trap_id;

public:
	TrapManager(PVOID lpAddressPage, PVOID lpFlagPage, HANDLE hRequestEvent, HANDLE ReplyEvent, bool IsWow64);

	~TrapManager();

	/* 提交，出错概率可能比较高？ */
	TrapResult commit();

	/* 删除，应该是不会出错？ */
	TrapResult decommit(bool AllOfThem);

	/* 失败返回-1，成功返回陷阱ID */
	int check_existing(PVOID address);

	/* 需要过滤掉无效项 */
	PVOID query_address(int index);

	/* 统计信息获取 */
	unsigned int committed_count();

private:

	PVOID _get_address(WORD SlotIndex);
	void _set_address(PVOID Address, WORD SlotIndex);

	/* 生成错误报告，填充列表 */
	void _generate_error_report();

	/* 等待，可能会失败 */
	bool _request_and_wait();

};