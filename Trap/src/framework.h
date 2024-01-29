#pragma once

#include <windows.h>
#include <stdio.h>

#include "../../shared/r_structs.h"

/* 第三方钩子库 */
#include "C:/Users/86158/source/repos/Third_Party_Libs/Detours-4.0.1/include/detours.h"
#ifdef _WIN64
#pragma comment(lib, "C:\\Users\\86158\\source\\repos\\Third_Party_Libs\\Detours-4.0.1\\lib.X64\\detours.lib")
#else
#pragma comment(lib, "C:\\Users\\86158\\source\\repos\\Third_Party_Libs\\Detours-4.0.1\\lib.X86\\detours.lib")
#endif

/* logger */
#define infoln(format, ...) printf("<r>" format "\n", ##__VA_ARGS__)
#define errln(format, ...) printf("<r> [ERR]" format "\n", ##__VA_ARGS__)
#define errln_ex(format, ...) printf("<r> [ERR %d]" format "\n", GetLastError(), ##__VA_ARGS__)
#define bugln(format, ...) printf("<r> [***BUG***]" format "\n", ##__VA_ARGS__)