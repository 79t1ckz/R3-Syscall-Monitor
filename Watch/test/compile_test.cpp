#include "cmd_application.h"
#include "../include/client.h"

#include <fstream>

#define TEST_EXE	"D://TraceMe.exe"

void print_module_list(const char* lpModuleName);

int main()
{
	system("chcp 65001");

	//STARTUPINFOA sia;
	//PROCESS_INFORMATION pi;
	//ZeroMemory(&sia, sizeof(sia));

	//CreateProcessA(
	//	TEST_EXE,
	//	NULL,
	//	NULL,
	//	NULL,
	//	false,
	//	0,
	//	NULL,
	//	NULL,
	//	&sia,
	//	&pi);

	//Sleep(500);

	//ModuleParser parser(pi.hProcess);
	//g_parser = &parser;
	//
	//print_module_list("user32.dll");

	//std::map<PVOID, std::string> task_book;

	//init_task_loading(&parser);
	//load_tasks_from_file("ntuser.txt", &task_book);

	///* 打印测试数据 */
	//auto ite = task_book.begin();
	//while (ite != task_book.end()) {
	//	printf("\t%p\t%s\n", ite->first, ite->second.c_str());
	//	ite++;
	//}

	///* 生成测试数据 */
	//EdtIter iter(p_nt, EdtIter::By_Name);
	//std::fstream writer("D://ntuser.txt", std::ios::out);

	//writer << "<win32u.dll>\n";
	//while (parser.ite(&iter)) {

	//	if (iter.name.find("NtUser") != 0)
	//		continue;

	//	writer << "\t\"" << iter.name << "\"\n";
	//}
	//writer << "</win32u.dll>\n";

	return 0;
}