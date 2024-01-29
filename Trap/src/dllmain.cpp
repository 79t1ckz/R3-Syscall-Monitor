#include <windows.h>

bool g_process_is_exiting = false;
DWORD g_worker_id = -1;

static HANDLE s_h_worker = NULL;

extern HANDLE g_request_event;

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;

    /* 进程退出 */
    case DLL_PROCESS_DETACH:

        s_h_worker = OpenThread(THREAD_ALL_ACCESS, false, g_worker_id);
        if (s_h_worker == NULL)
            break;

        g_process_is_exiting = true;
        SetEvent(g_request_event);
        WaitForSingleObject(s_h_worker, 4000);
        CloseHandle(s_h_worker);
        break;
    }
    return TRUE;
}