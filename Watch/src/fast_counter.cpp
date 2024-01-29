#include "fast_counter.h"
#include <algorithm>

FastCounter::FastCounter(PVOID pCountPage)
{
	this->count_page = (DWORD*)pCountPage;
	this->shadow_count_page = new DWORD[MaxSlotCount];

	this->pick_type = PickType::So_Far;
	this->sort_type = SortType::Value_Top_Down;

	this->last_update_time = GetTickCount();

	ZeroMemory(this->shadow_count_page, MaxSlotCount * sizeof(DWORD));
}

FastCounter::~FastCounter()
{
	delete[] this->shadow_count_page;
}

/*
	 在此函数中操作shadow表
*/
void FastCounter::update()
{
	int i;
	DWORD cur_update_time = GetTickCount();
	CallReportEntry report_entry;
	this->counter_report.clear();

	/* 获取数据 */
	for (i = 0; i < MaxSlotCount; i++) {
		if (this->pick_type == PickType::So_Far) {
			if (this->count_page[i] == 0) continue;
			report_entry.u.count_all_time = count_page[i];
		}
		else if (this->pick_type == PickType::Recently_Freq) {
			if (this->count_page[i] <= this->shadow_count_page[i]) continue;
			report_entry.u.freq = (count_page[i] - shadow_count_page[i]) * 1000.0 / (cur_update_time - last_update_time);
		}
		else {
			if (this->count_page[i] <= this->shadow_count_page[i]) continue;
			report_entry.u.count = count_page[i] - shadow_count_page[i];
		}
		report_entry.trap_id = i;
		this->counter_report.push_back(report_entry);
	}

	/* 更新shadow表 */
	for (i = 0; i < MaxSlotCount; i++) {
		this->shadow_count_page[i] = this->count_page[i];
	}
	last_update_time = cur_update_time;
}

class MySorter
{
private:
	PickType pick_type;
	SortType sort_type;

public:
	MySorter(PickType PickTy, SortType SortTy)
	{
		this->pick_type = PickTy;
		this->sort_type = SortTy;
	}

	/* 返还值：是否左边的参数应该排在前面 */
	bool operator () (CallReportEntry& first, CallReportEntry& next)
	{
		bool is_first_one_better = true;
		switch (sort_type) {
		case SortType::By_Id:
			is_first_one_better = first.trap_id < next.trap_id;
			break;
		case SortType::Value_Top_Down:	// 根据值的不同进行排序
			switch (pick_type) {
			case PickType::Recently:
				is_first_one_better = first.u.count > next.u.count;
				break;
			case PickType::Recently_Freq:
				is_first_one_better = first.u.freq > next.u.freq;
				break;
			case PickType::So_Far:
				is_first_one_better = first.u.count_all_time > next.u.count_all_time;
				break;
			}break;
		case SortType::Freq_Close_To_60x:
			is_first_one_better = _delta(first.u.freq, 60.0) < _delta(next.u.freq, 60.0);
			break;
		case SortType::Freq_Close_To_10x:
			is_first_one_better = _delta(first.u.freq, 10.0) < _delta(next.u.freq, 10.0);
			break;
		}

		return is_first_one_better;
	}

private:

	/* 要求：计算出值到特定倍数之间的距离（0不算） */
	float _delta(float value, float param)
	{
		float round_val = round(value / param);
		if (round_val < 0.001) {
			round_val = 1.0;
		}
		return fabs(round_val * param - value);
	}

};

/*
	排序。
	默认排序：陷阱ID
	如果排序类型无效，直接返回
*/
void FastCounter::sort()
{
	if ((this->sort_type == SortType::Freq_Close_To_60x || this->sort_type == SortType::Freq_Close_To_10x) &&
		this->pick_type != PickType::Recently_Freq) {
		return;
	}

	std::sort(
		this->counter_report.begin(),
		this->counter_report.end(),
		MySorter(this->pick_type, this->sort_type));
}