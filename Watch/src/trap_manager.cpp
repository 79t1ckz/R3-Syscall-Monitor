#include "trap_manager.h"


TrapManager::TrapManager(PVOID lpAddressPage, PVOID lpFlagPage, HANDLE hRequestEvent, HANDLE hReplyEvent, bool IsWow64)
{
	/* 必要参数 */
	this->is_processing_wow64 = IsWow64;
	this->request_event = hRequestEvent;
	this->reply_event = hReplyEvent;
	this->address_page = lpAddressPage;
	this->flag_page = (BYTE*)lpFlagPage;

	/* 自行生成 */
	this->shadow_address_page = new PVOID[MaxSlotCount];
}

TrapManager::~TrapManager()
{
	delete[] this->shadow_address_page;
}

PVOID TrapManager::_get_address(WORD SlotIndex)
{
	//if (this->is_processing_wow64) {
	//	return (PVOID) * ((DWORD*)this->address_page + SlotIndex);
	//}
	//else
	//	return (PVOID) * ((PVOID*)this->address_page + SlotIndex);

	return this->shadow_address_page[SlotIndex];
}

void TrapManager::_set_address(PVOID Address, WORD SlotIndex)
{
	if (this->is_processing_wow64) {
		*((DWORD*)this->address_page + SlotIndex) = (DWORD)Address;
	}
	else {
		*((PVOID*)this->address_page + SlotIndex) = Address;
	}

	this->shadow_address_page[SlotIndex] = Address;
}

/* 操作：1.修正标志页。2.生成报告表 */
void TrapManager::_generate_error_report()
{
	int i;
	for (i = 0; i < MaxSlotCount; i++)
	{
		if (this->flag_page[i] & TF_ChangeRequest) {
			if (this->flag_page[i] & TF_Error) {
				this->tasks_failed.push_back(_get_address(i));
				this->flag_page[i] &= ~TF_Error;
			}
			else
				this->tasks_pending.push_back(_get_address(i));
			this->flag_page[i] &= ~TF_ChangeRequest;
		}
	}
}

//extern HANDLE s_reply_event;

bool TrapManager::_request_and_wait()
{
	SetEvent(this->request_event);

	return WaitForSingleObject(this->reply_event, 1000) == WAIT_OBJECT_0;
}

int TrapManager::check_existing(PVOID address)
{
	auto ite = this->address_to_trap_id.find(address);
	if (ite == this->address_to_trap_id.end())
		return -1;
	else
		return ite->second;
}

/* 给出插桩地址 */
PVOID TrapManager::query_address(int index)
{
	if (index < 0 || index >= MaxSlotCount)
		return NULL;

	if ((this->flag_page[index] & TF_Using) == false)
		return NULL;

	return this->_get_address(index);
}

unsigned int TrapManager::committed_count()
{
	return this->address_to_trap_id.size();
}

/*
	提交。
	1. 成功，tasks_pending会被清空。
	2. 槽位不够，tasks_pending会保持原样。
	3. 等待超时，tasks_pending会保持原样。
	4. 拦截出错，tasks_failed显示出错项，tasks_pending显示有效项。

*/
TrapManager::TrapResult TrapManager::commit()
{
	WORD i;
	WORD cur_free_slot = 0;
	PVOID cur_trap_address;
	TrapResult retn_result;
	std::pair<PVOID, WORD> trap_info;

	/* 首先检查异常输入 */


	/* 生成有效的任务 */
	for (i = 0; i < this->tasks_pending.size(); i++) {

		cur_trap_address = this->tasks_pending[i];

		/* 首先不能提交已有项 */
		if (this->check_existing(cur_trap_address) != -1)
			continue;

		/* 查找有效项 */
		while (cur_free_slot < MaxSlotCount) {
			if ((this->flag_page[cur_free_slot] & TF_Using) == false)
				break;
			cur_free_slot++;
		}

		if (cur_free_slot >= MaxSlotCount) {
			retn_result = Not_Enough_Free_Slot;
			goto EXIT;
		}

		trap_info.first = cur_trap_address;
		trap_info.second = cur_free_slot;

		this->tasks_doing.push_back(trap_info);
		cur_free_slot++;
	}

	/* 开始写入 */
	for (i = 0; i < this->tasks_doing.size(); i++)
	{
		cur_trap_address = this->tasks_doing[i].first;
		cur_free_slot = this->tasks_doing[i].second;

		if (this->flag_page[cur_free_slot] & TF_Using) {
			printf("[BUG] tried to commit a committed ?? slot = %d\n", cur_free_slot);
		}

		_set_address(cur_trap_address, cur_free_slot);
		this->flag_page[cur_free_slot] |= TF_ChangeRequest;
	}

	/* 等待结果。。 */
	if (!_request_and_wait()) {
		retn_result = Time_Out;
		goto EXIT;
	}

	/* 检查错误 */
	this->tasks_pending.clear();
	this->tasks_failed.clear();
	_generate_error_report();
	if (this->tasks_failed.size()) {
		retn_result = Detour_Failure;
		goto EXIT;
	}

	if (this->tasks_pending.size())
		printf("still have pending tasks??\n");

	/* 更新数据 */
	for (i = 0; i < this->tasks_doing.size(); i++) {
		cur_trap_address = this->tasks_doing[i].first;
		cur_free_slot = this->tasks_doing[i].second;

		this->address_to_trap_id.emplace(cur_trap_address, cur_free_slot);
	}

	retn_result = No_Error;

EXIT:
	this->tasks_doing.clear();
	return retn_result;
}

/*
	删除。
	1. 成功，清空tasks_pending
	2. 失败。被忽略（因为依旧可以正常工作）

*/
TrapManager::TrapResult TrapManager::decommit(bool AllOfThem)
{
	WORD i;
	int cur_valid_slot;
	PVOID cur_trap_address;
	TrapResult retn_result;
	std::pair<PVOID, WORD> trap_info;

	/* 生成表单 */
	if (AllOfThem) {
		auto ite_0 = this->address_to_trap_id.begin();
		while (ite_0 != this->address_to_trap_id.end()) {
			trap_info.first = ite_0->first;
			trap_info.second = ite_0->second;
			this->tasks_doing.push_back(trap_info);
			ite_0++;
		}
	}
	else {
		for (i = 0; i < this->tasks_pending.size(); i++) {
			cur_trap_address = this->tasks_pending[i];
			cur_valid_slot = check_existing(cur_trap_address);
			if (cur_valid_slot == -1)
				continue;
			trap_info.first = cur_trap_address;
			trap_info.second = cur_valid_slot;
			this->tasks_doing.push_back(trap_info);
		}
	}

	/* 提交表单 */
	for (i = 0; i < this->tasks_doing.size(); i++) {
		cur_trap_address = this->tasks_doing[i].first;
		cur_valid_slot = this->tasks_doing[i].second;

		if ((this->flag_page[i] & TF_Using) == false) {
			printf("[BUG] double free ? address = %p\n", this->tasks_doing[i].first);
			continue;
		}

		this->flag_page[i] |= TF_ChangeRequest;
	}

	/* 等待回复 */
	if (!_request_and_wait()) {
		retn_result = Time_Out;
		goto EXIT;
	}

	/* 检查错误 */
	this->tasks_pending.clear();
	this->tasks_failed.clear();
	_generate_error_report();
	if (this->tasks_failed.size()) {
		retn_result = Detour_Failure;
		goto EXIT;
	}

	if (this->tasks_pending.size())
		printf("<MAY BE BUG> still have pending tasks??\n");

	/* 删除键值对 */
	for (i = 0; i < this->tasks_doing.size(); i++) {
		cur_trap_address = this->tasks_doing[i].first;
		this->address_to_trap_id.erase(cur_trap_address);
	}

	retn_result = No_Error;

EXIT:
	this->tasks_doing.clear();
	return retn_result;
}