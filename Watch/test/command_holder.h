#pragma once

#include <windows.h>

/*

	可复用的命令行架构

*/

#define CMD_BUFF_SIZE	512
#define CMD_COUNT		32

class CommandHolder
{
private:
	int arg_index;
	int argc;
	char* argv[CMD_COUNT];
	char buff[CMD_BUFF_SIZE];

private:
	~CommandHolder();

public:
	CommandHolder();
	void recv();
	void seek(int Step);

	/* 不会报错 */
	const char* try_get();
	bool try_parse_as_u32(DWORD& Num, int NumType);
	bool try_end();
	bool match(const char* lpArg);

	/* 不会报错 */
	const char* get();
	DWORD parse_as_u32(int NumType);
	void assert_end();

	/* 测试专用 */
	void print_args();
};