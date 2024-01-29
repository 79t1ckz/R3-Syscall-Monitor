#include "command_holder.h"

#include <stdexcept>
#include <string>

/*
 *
 *	文件作用：提供最为基本的命令行支持。
 * 
 */

CommandHolder::~CommandHolder()
{
}

CommandHolder::CommandHolder()
{
	argc = 0;
	arg_index = -1;
}

/*

	解析规则：按照可见符号解析
	可能会报错

*/
void CommandHolder::recv()
{
	int is_last_one_valid = false;
	int i;
	int len;

	fgets(this->buff, CMD_BUFF_SIZE, stdin);
	rewind(stdin);

	/* 状态清除 */
	this->argc = 0;
	this->arg_index = 0;

	len = strlen(this->buff);
	if (len + 1 >= CMD_BUFF_SIZE) {
		throw std::exception("the input is too long! what we got may be incomplete");
	}

	/* 指令切割 */
	for (i = 0; i < len; i++) {

		if (this->argc > CMD_COUNT) {
			throw std::exception("too much args! (max count: 32)");
		}

		/* 可见字符（要剔除空格） */
		if (this->buff[i] >= 33 && this->buff[i] <= 126) {
			if (is_last_one_valid == false) {
				this->argv[this->argc] = this->buff + i;
			}
			is_last_one_valid = true;
		}
		else {
			if (is_last_one_valid == true) {
				this->buff[i] = '\0';
				this->argc++;
			}
			is_last_one_valid = false;
		}
	}
}

void CommandHolder::seek(int Step)
{
	arg_index += Step;
	if (arg_index < 0) arg_index = 0;
	if (arg_index > argc) arg_index = argc;
}

const char* CommandHolder::try_get()
{
	if (this->arg_index + 1 >= this->argc ||
		this->arg_index < 0) {
		return NULL;
	}
	else {
		return this->argv[++this->arg_index];
	}
}

bool CommandHolder::try_parse_as_u32(DWORD& Num, int NumType)
{
	size_t checker;
	try {
		Num = std::stoul(this->argv[this->arg_index], &checker, NumType);
		if (checker != strlen(this->argv[this->arg_index])) {
			throw std::exception();
		}
	}
	catch (std::exception& e) {
		return false;
	}

	return true;
}

bool CommandHolder::try_end()
{
	return this->arg_index + 1 >= this->argc;
}

bool CommandHolder::match(const char* lpArg)
{
	if (this->arg_index >= this->argc ||
		this->arg_index < 0) {
		return false;
	}

	return strcmp(lpArg, this->argv[this->arg_index]) == 0;
}

//
// 会报错
//

const char* CommandHolder::get()
{
	if (this->arg_index >= this->argc ||
		this->arg_index < 0) {
		throw std::exception("Missing args");
	}

	return this->argv[++this->arg_index];
}

DWORD CommandHolder::parse_as_u32(int NumType)
{
	DWORD num;
	size_t checker;

	num = std::stoul(this->argv[this->arg_index], &checker, NumType);
	if (checker != strlen(this->argv[this->arg_index])) {
		throw std::exception("bad digit format");
	}

	return num;
}

void CommandHolder::assert_end()
{
	if (this->arg_index + 1 < this->argc) {
		throw std::exception("extra not-needed args");
	}
}

void CommandHolder::print_args()
{
	printf("Here are args:\n");
	for (int i = 0; i < this->argc; i++) {
		printf("\t%d\t%s\n", i, this->argv[i]);
	}
}