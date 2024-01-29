#include "../include/client.h"
#include "cmd_application.h"
#include "command_holder.h"

#include <locale.h>
#include <iostream>

/*
 *
 *	文件作用：提供命令行用户接口
 *	最基本的命令行处理逻辑
 * 
 */
static bool s_exit_flag = false;
static int s_reader;
static char s_help_info_1[] = R"EOF(
What can we do while we don't have a watch target:

	help	show help informations
	
	state	show watcher's current state
	
	list
		proc	list available processes
			with-wnd	only list who has window
			[ ]			list all

	attach [id]		attach to the target
		log			enable logger module

	quit	exit process

)EOF";

static char s_help_info_2[] = R"EOF(
What can we do with a target:

	help	show help informations
	
	state	show watcher's current state
	
	list
		module		list modules in the target
		set
			<name>	list task entrys in the task set
			<null>	list committed tasks
		tasks		list hot task entrys
		tracing		list tracing tasks

	detach		detach from the target
	
	hook <set>	hook (or rehook) a task set
	unhook		do unhooks
		<set>	unhook a task set
		<null>		unhook all tasks

	counter 
		pick [cnt/frq/cnt_all]	pick what kind of data
		sort [id/value/10x/60x]	sort by which rule
		data/<null>				show fast counter data

	logger
		on				start logging
		off				stop logging
		cooked			show stacked data
		raw [rva]		show raw data
		query <hash>	query hash and show chain
		clear			clear records
	
	trace
		+ id+			disable tracing
		- id+			enable tracing

)EOF";

static CommandHolder* p_holder;

/*
	指令分类方式，请使用状态机分类：
	1. 未注入前
	2. 已注入
*/

/* 指令解释函数（无目标） */
static void execute_command_state_1()
{
	/* 打印帮助信息 */
	if (p_holder->match("help")) {
		p_holder->assert_end();
		system("cls");
		printf("%s", s_help_info_1);
		return;
	}
	/* 状态信息 */
	else if (p_holder->match("state")) {
		p_holder->assert_end();
		system("cls");
		print_state();
		return;
	}
	/* 基本信息列举 */
	else if (p_holder->match("list")) {
		p_holder->get();
		/* 打印进程列表 */
		if (p_holder->match("proc")) {
			if (p_holder->try_get() == NULL) {
				print_proc_list(false);
				return;
			}
			p_holder->assert_end();
			if (p_holder->match("with-wnd")) {
				print_proc_list(true);
				return;
			}
		}
	}
	/* 附加 */
	else if (p_holder->match("attach")) {
		DWORD pid;
		p_holder->get();
		pid = p_holder->parse_as_u32(10);
		if (p_holder->try_get() == NULL) {
			attach_to_target(pid, false);
			return;
		}
		p_holder->assert_end();
		if (p_holder->match("log")) {
			attach_to_target(pid, true);
			return;
		}
	}
	/* 退出 */
	else if (p_holder->match("quit")) {
		s_exit_flag = true;
		return;
	}

	throw std::exception("unknown command");
}

static void check_logger()
{
	if (g_reader == NULL) {
		throw std::exception("Trap&Trace module is not inited!");
	}
}

/* 指令解释函数（有目标） */
static void execute_command_state_2()
{
	const char* lp_temp;

	/* 打印帮助信息 */
	if (p_holder->match("help")) {
		p_holder->assert_end();
		system("cls");
		printf("%s", s_help_info_2);
		return;
	}
	/* 状态信息 */
	else if (p_holder->match("state")) {
		p_holder->assert_end();
		system("cls");
		print_state();
		return;
	}
	/* 基本信息列举 */
	else if (p_holder->match("list")) {
		p_holder->get();
		/* 打印模块列表 */
		if (p_holder->match("module")) {
			lp_temp = p_holder->try_get();
			p_holder->assert_end();
			print_module_list(lp_temp);
			return;
		}
		/* 打印任务集信息 */
		else if (p_holder->match("set")) {
			lp_temp = p_holder->try_get();
			p_holder->assert_end();
			print_set_list(lp_temp);
			return;
		}
		/* 打印热任务 */
		else if (p_holder->match("tasks")) {
			p_holder->assert_end();
			print_hot_tasks();
			return;
		}
		else if (p_holder->match("tracing")) {
			check_logger();
			p_holder->assert_end();
			print_tracing_list();
			return;
		}
	}
	/* 脱离 */
	else if (p_holder->match("detach")) {
		p_holder->assert_end();
		detach_from_target();
		return;
	}
	/* 钩取 */
	else if (p_holder->match("hook")) {
		lp_temp = p_holder->get();
		p_holder->assert_end();
		commit_task_set(lp_temp);
		return;
	}
	/* 卸载 */
	else if (p_holder->match("unhook")) {
		lp_temp = p_holder->try_get();
		p_holder->assert_end();
		decommit_task_set(lp_temp);
		return;
	}
	/* 计数器 */
	else if (p_holder->match("counter")) {
		lp_temp = p_holder->try_get();
		if (lp_temp == NULL) {
			print_counter_stats();
			return;
		}
		else if (p_holder->match("data")) {
			p_holder->assert_end();
			print_counter_stats();
			return;
		}
		else if (p_holder->match("pick")) {
			lp_temp = p_holder->get();
			p_holder->assert_end();
			set_counter_pick_type(lp_temp);
			return;
		}
		else if (p_holder->match("sort")) {
			lp_temp = p_holder->get();
			p_holder->assert_end();
			set_counter_sort_type(lp_temp);
			return;
		}
	}
	/* 日志记录器相关 */
	else if (p_holder->match("logger")) {
		check_logger();
		//lp_temp = p_holder->try_get();
		//if (lp_temp == NULL) {
		//	print_log_data();
		//	return;
		//}
		//p_holder->assert_end();
		//if (p_holder->match("data")) {
		//	lp_temp = p_holder->try_get();
		//	if (lp_temp == NULL) {
		//		print_log_data();
		//		return;
		//	}
		//	if (p_holder->match("raw")) {
		//		DWORD start_rva;
		//		p_holder->get();
		//		start_rva = p_holder->parse_as_u32(10);
		//		p_holder->assert_end();
		//		print_raw_log_data(start_rva);
		//		return;
		//	}
		//}
		p_holder->get();
		if (p_holder->match("cooked")) {
			p_holder->assert_end();
			print_log_data();
			return;
		}
		else if (p_holder->match("raw")) {
			p_holder->get();
			DWORD start_rva = p_holder->parse_as_u32(10);
			p_holder->assert_end();
			print_raw_log_data(start_rva);
			return;
		}
		else if (p_holder->match("clear")) {
			p_holder->assert_end();
			g_reader->clear();
			return;
		}
		else if (p_holder->match("on")) {
			p_holder->assert_end();
			set_log_reader(true);
			return;
		}
		else if (p_holder->match("off")) {
			p_holder->assert_end();
			set_log_reader(false);
			return;
		}
		else if (p_holder->match("query")) {
			DWORD hash;
			p_holder->get();
			hash = p_holder->parse_as_u32(16);
			p_holder->assert_end();
			print_hash_record(hash);
			return;
		}
	}
	/* 追踪 */
	else if (p_holder->match("trace")) {
		check_logger();
		p_holder->get();
		std::vector<DWORD> trace_list;
		if (p_holder->match("+")) {
			while (p_holder->try_get()) {
				trace_list.push_back(p_holder->parse_as_u32(10));
			}
			enable_tracing(trace_list);
			return;
		}
		else if (p_holder->match("-")) {
			while (p_holder->try_get()) {
				trace_list.push_back(p_holder->parse_as_u32(10));
			}
			disable_tracing(trace_list);
			return;
		}
	}

	throw std::exception("unknown command");
}

int main()
{
	setlocale(LC_ALL, "");

	p_holder = new CommandHolder();
	printf("Please input commands:\n");

	/* 解释执行 */
	do {
		try {
			p_holder->recv();
			if (g_parser == NULL) {
				execute_command_state_1();
			}
			else {
				execute_command_state_2();
			}
		}
		catch (std::exception& e) {
			printf("<CommandError> Reason: %s\n", e.what());
		}

	} while (s_exit_flag == false);

	return 0;
}