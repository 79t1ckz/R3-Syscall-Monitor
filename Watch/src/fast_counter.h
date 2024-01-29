#pragma once

#include <windows.h>
#include <vector>
#include "../../shared/r_structs.h"

typedef struct {

	DWORD trap_id;
	union {
		DWORD count;
		float freq;
		DWORD count_all_time;
	}u;

}CallReportEntry;

enum class SortType {
	By_Id,			// 默认
	Value_Top_Down,
	Freq_Close_To_60x,
	Freq_Close_To_10x		// 可以用于搜索 20x 30x 60x 120x
};

enum class PickType {
	Recently,		// 从上一次开始
	Recently_Freq,
	So_Far
};

class FastCounter
{
private:
	DWORD* count_page;			// 外部参数
	DWORD* shadow_count_page;	// 内部自行申请
	DWORD last_update_time;		// 用于生成频率

public:
	PickType pick_type;
	SortType sort_type;
	std::vector<CallReportEntry> counter_report;

public:
	FastCounter(PVOID lpCountPage);
	~FastCounter();

	void update();
	void sort();
};
