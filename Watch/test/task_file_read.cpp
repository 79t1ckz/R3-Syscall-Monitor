#include "cmd_application.h"
#include "../include/client.h"

#include <windows.h>
#include <map>
#include <string>
#include <fstream>
#include <format>

/* 
	文件作用——解析任务描述文件

	任务格式：
		<module>
			"MessageBoxA"	"消息对话框"
			#1234			"可疑函数"
			+0x12364		"可疑地址"
		</module>
	
	1. 注释可以不给出，不给出则进行自动的生成
	2. 语句后必须存在换行符
	3. 换行符会被折叠

*/

#define TASK_READ_FILE	L"\\tasks\\"

enum class MyToken
{
	Module_Start,
	Module_End,
	String,
	Oridinal,
	Offest,
	Line_Break,
	EoF
};

/* 文件读取相关 */
static MyToken cur_token;
static std::string cur_token_str;
static DWORD cur_token_num;
static std::fstream reader;

/* 文件选取相关 */
static wchar_t task_read_directory[MAX_PATH] = { 0 };

/* 任务生成相关 */
// static ModuleParser* g_parser = NULL;
static std::map<PVOID, std::string>* p_task_book;

/* 上下文 */
static DWORD cur_line;
static ModuleInfo* cur_module;
static std::string cur_module_name;

///* 初始化函数 */
//void init_task_loading(ModuleParser* Parser)
//{
//	g_parser = Parser;
//	GetCurrentDirectoryW(MAX_PATH, task_read_directory);
//	wcscat_s(task_read_directory, MAX_PATH, TASK_READ_FILE);
//}

/* 遇到末尾会报错 */
static char strict_get_char()
{
	int ch = reader.get();
	if (ch == -1)
		throw std::exception("unexcepted EoF");
	return ch;
}

static void strict_push_back(char ch)
{
	if (cur_token_str.size() > MAX_PATH)
		throw std::exception("string is too long");
	if (ch == '\n')
		throw std::exception("cross-line string is not allowed");
	cur_token_str.push_back(ch);
}

static bool is_next_seperator()
{
	return (reader.peek() == '\t' ||
		reader.peek() == ' ' ||
		reader.peek() == '\n' ||
		reader.peek() == -1);
}

/* 获取下一个标识符 */
static void get_next_token()
{
	int ch;
	cur_token_str.clear();
	do {
		ch = reader.get();

		/* 结束符 */
		if (ch == -1) {
			cur_token = MyToken::EoF;
			break;
		}
		/* 注释 */
		else if (ch == '/' && reader.peek() == '/') {
			reader.seekg(1, std::ios::cur);
			do {
				ch = reader.get();
			} while (ch != '\n' && ch != -1);
			reader.seekg(-1, std::ios::cur);
		}
		/* 模块起始符/终止符 */
		else if (ch == '<') {
			ch = strict_get_char();
			if (ch == '/') {
				cur_token = MyToken::Module_End;
				ch = strict_get_char();
			}
			else {
				cur_token = MyToken::Module_Start;
			}
			while (ch != '>') {
				strict_push_back(ch);
				ch = strict_get_char();
			}
			break;
		}
		/* 字符串 */
		else if (ch == '"') {
			cur_token = MyToken::String;
			ch = strict_get_char();
			while (ch != '"') {
				strict_push_back(ch);
				ch = strict_get_char();
			}
			break;
		}
		/* 偏移值 */
		else if (ch == '+') {
			cur_token = MyToken::Offest;
			while (is_next_seperator() == false) {
				ch = strict_get_char();
				strict_push_back(ch);
			}
			break;
		}
		/* 序列号 */
		else if (ch == '#') {
			cur_token = MyToken::Oridinal;
			while (is_next_seperator() == false) {
				ch = strict_get_char();
				strict_push_back(ch);
			}
			break;
		}
		/* 换行符 */
		else if (ch == '\n') {
			cur_line++;
			// 折叠换行符
			if (cur_token == MyToken::Line_Break ||
				cur_token == MyToken::EoF)
				continue;
			cur_token = MyToken::Line_Break;
			break;
		}
		else if (ch == ' ' || ch == '\t') {
			continue;
		}
		else {
			throw std::exception("unexcepted character");
		}

	} while (1);

	/* 数字转换尝试 */
	if (cur_token == MyToken::Oridinal ||
		cur_token == MyToken::Offest) {
		size_t checker;
		cur_token_num = std::stoul(cur_token_str, &checker, 16);
		if (checker != cur_token_str.size()) {
			throw std::exception("bad digit format");
		}
	}
}

/* 解析任务项 */
static void parse_task_entry()
{
	PVOID task_addr;
	std::string task_comment;

	/* 针对无效模块的处理 */
	if (cur_module == NULL) {
		do {
			get_next_token();
			if (cur_token != MyToken::Line_Break &&
				cur_token != MyToken::String &&
				cur_token != MyToken::Oridinal &&
				cur_token != MyToken::Offest) {
				break;
			}
		} while (1);
		return;
	}

	do {
	
		get_next_token();

		/* 函数名 */
		if (cur_token == MyToken::String) {
			task_addr = g_parser->getProcAddr(cur_module, cur_token_str.c_str(), NULL);
			task_comment = cur_token_str;
			get_next_token();
			if (cur_token == MyToken::String) {
				task_comment = cur_token_str;
				get_next_token();
			}
		}
		/* 序列号 */
		else if (cur_token == MyToken::Oridinal) {
			task_addr = g_parser->getProcAddr(cur_module, cur_token_num, NULL);
			task_comment = cur_module_name + std::format("#0x{0:X}", cur_token_num);
			get_next_token();
			if (cur_token == MyToken::String) {
				task_comment = cur_token_str;
				get_next_token();
			}
		}
		/* 模块偏移地址 */
		else if (cur_token == MyToken::Offest) {
			task_addr = (PCHAR)cur_module->image_base + cur_token_num;
			task_comment = cur_module_name + std::format("+0x{0:X}", cur_token_num);
			get_next_token();
			if (cur_token == MyToken::String) {
				task_comment = cur_token_str;
				get_next_token();
			}
		}
		else {
			break;
		}

		/* 成功解析，匹配换行符 */
		if (cur_token != MyToken::Line_Break) {
			throw std::exception("missing line-break");
		}

		/* 任务定位结果处理（已匹配换行符，故在上一行） */
		if (task_addr) {
			if (p_task_book->size() >= 4096) {
				throw std::exception("too mush task entrys! (>=4096)");
			}
			auto ite = p_task_book->find(task_addr);
			if (ite != p_task_book->end()) {
				printf("<warn> Line: %d - task is existing, comment = %s\n", cur_line - 1, ite->second.c_str());
			}
			else {
				p_task_book->emplace(task_addr, task_comment);
			}
		}
		else {
			printf("<warn> Line: %d - failed to locate task, comment = %s\n", cur_line - 1, task_comment.c_str());
		}

	} while (1);
}

/* 解析模块项 := <起始符> [<任务项>]* <终止符> */
static void parse_module_block()
{
	do {
		/* 匹配起始符 / 文件结束符 / 换行符 */
		get_next_token();
		if (cur_token == MyToken::EoF) {
			break;
		}
		else if (cur_token == MyToken::Line_Break) {
			continue;
		}
		else if (cur_token != MyToken::Module_Start) {
			throw std::exception("missing module-start");
		}

		cur_module_name = cur_token_str;
		if (cur_token_str == "exe") {
			cur_module = g_parser->exe_info();
		}
		else {
			cur_module = g_parser->queryModule(cur_token_str.c_str());
		}
		if (cur_module == NULL) {
			printf("<warn> Line: %d - can't find module %s\n", cur_line, cur_token_str.c_str());
		}

		/* 匹配换行符 */
		get_next_token();
		if (cur_token != MyToken::Line_Break) {
			throw std::exception("missing line-break");
		}

		parse_task_entry();

		/* 匹配终止符 */
		if (cur_token != MyToken::Module_End) {
			throw std::exception("missing module-end");
		}
		else if (cur_token_str != cur_module_name) {
			throw std::exception("module-end dismatch");
		}

		cur_module = NULL;

	} while (1);
}

static void print_tokens()
{
	do {
		
		get_next_token();
		switch (cur_token) {
		case MyToken::EoF:
			printf("\tEof\n");
			break;
		case MyToken::Line_Break:
			printf("\tLine Break\n");
			break;
		case MyToken::Module_End:
			printf("\tMod End: %s\n", cur_token_str.c_str());
			break;
		case MyToken::Module_Start:
			printf("\tMod Start: %s\n", cur_token_str.c_str());
			break;
		case MyToken::String:
			printf("\tString: %s\n", cur_token_str.c_str());
			break;
		case MyToken::Offest:
			printf("\tOffest: 0x%X\n", cur_token_num);
			break;
		case MyToken::Oridinal:
			printf("\tOridinal: 0x%X\n", cur_token_num);
			break;
		}

	} while (cur_token != MyToken::EoF);
}

static void skip_BOM()
{
	if (reader.get() == 0xEF &&
		reader.get() == 0xBB &&
		reader.get() == 0xBF) {
		;
	}
	else {
		reader.seekg(0, std::ios::beg);
	}
}

/* 核心生成函数 */
bool load_tasks_from_file(const char* lpFileName, std::map<PVOID, std::string>* lpTaskBook)
{
	DWORD length;
	wchar_t* p_name_w;
	std::wstring p_target_w;

	if (task_read_directory[0] == 0) {
		GetCurrentDirectoryW(MAX_PATH, task_read_directory);
		wcscat_s(task_read_directory, MAX_PATH, TASK_READ_FILE);
	}

	/* 拼出字符串 */
	length = MultiByteToWideChar(CP_UTF8, 0, lpFileName, -1, NULL, 0);
	if (length == 0) {
		errln("bad file name");
		return false;
	}

	p_name_w = new wchar_t[length];
	MultiByteToWideChar(CP_UTF8, 0, lpFileName, -1, p_name_w, length);
	p_target_w = task_read_directory;
	p_target_w += p_name_w;
	delete[] p_name_w;

	reader.open(p_target_w, std::ios::in);
	if (reader.is_open() == false) {
		wprintf(L"[ERR] failed to open file: %s", p_target_w.c_str());
		return false;
	}

	/* 开始读取文件 */
	cur_line = 1;
	cur_module = NULL;
	cur_token = MyToken::EoF;
	cur_token_str.clear();
	g_parser->walkAddressSpace();
	p_task_book = lpTaskBook;
	try {
		skip_BOM();
		parse_module_block();
	}
	catch (std::exception& e) {
		printf("<Read Failed> Line: %d Reason: %s\n", cur_line, e.what());
		p_task_book->clear();
	}

	/* 清理工作 */
	p_task_book = NULL;
	reader.close();

	return true;
}