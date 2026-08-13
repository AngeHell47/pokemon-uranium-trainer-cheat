#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdarg.h>
#include "opt_speedhack.h"

#pragma comment(lib, "winmm.lib")

bool g_speedhack_enabled = false;
int  g_speedhack_value10 = 10; // 0..100 => 0.0..10.0 ; 10 = normal

static char g_ini_path[MAX_PATH] = {0};

static CRITICAL_SECTION s_cs;
static bool s_cs_init = false;

static DWORD s_real_base = 0;
static DWORD s_fake_base = 0;
static bool  s_bases_init = false;

static BYTE  s_saved_bytes[5] = {0};
static BYTE* s_target = NULL;
static BYTE* s_gateway = NULL;
static bool  s_hook_installed = false;

typedef DWORD (WINAPI *PFN_timeGetTime)(void);
static PFN_timeGetTime s_real_timeGetTime = NULL;

static void dbg(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;

    HANDLE h = CreateFileA("speedhack_debug.txt", FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wr;
        SetFilePointer(h, 0, 0, FILE_END);
        WriteFile(h, buf, lstrlenA(buf), &wr, NULL);
        WriteFile(h, "\r\n", 2, &wr, NULL);
        CloseHandle(h);
    }
}

static void save_cfg() {
    char buf[32];
    wsprintfA(buf, "%d", g_speedhack_enabled ? 1 : 0);
    WritePrivateProfileStringA("speedhack", "enabled", buf, g_ini_path);

    wsprintfA(buf, "%d", g_speedhack_value10);
    WritePrivateProfileStringA("speedhack", "value10", buf, g_ini_path);
}

static double current_factor_locked() {
    if (!g_speedhack_enabled) return 1.0;
    double f = (double)g_speedhack_value10 / 10.0;
    if (f < 0.0) f = 0.0;
    if (f > 10.0) f = 10.0;
    return f;
}

static void reset_bases_locked() {
    DWORD now = s_real_timeGetTime ? s_real_timeGetTime() : timeGetTime();
    s_real_base = now;
    s_fake_base = now;
    s_bases_init = true;
}

static DWORD compute_fake_time_locked() {
    DWORD now = s_real_timeGetTime ? s_real_timeGetTime() : timeGetTime();

    if (!s_bases_init) {
        s_real_base = now;
        s_fake_base = now;
        s_bases_init = true;
        return now;
    }

    double factor = current_factor_locked();
    DWORD real_delta = now - s_real_base;

    if (factor <= 0.0) {
        return s_fake_base;
    }

    return s_fake_base + (DWORD)((double)real_delta * factor);
}

extern "C" __declspec(naked) void hook_timeGetTime_stub() {
    __asm {
        pushad
        pushfd
    }

    DWORD ret = 0;
    if (s_cs_init) {
        EnterCriticalSection(&s_cs);
        ret = compute_fake_time_locked();
        LeaveCriticalSection(&s_cs);
    } else {
        ret = s_real_timeGetTime ? s_real_timeGetTime() : timeGetTime();
    }

    __asm {
        mov eax, ret
        popfd
        popad
        ret
    }
}

static bool install_inline_hook() {
    if (s_hook_installed) return true;

    if (!s_cs_init) {
        InitializeCriticalSection(&s_cs);
        s_cs_init = true;
    }

    HMODULE hWinmm = GetModuleHandleA("winmm.dll");
    if (!hWinmm) hWinmm = LoadLibraryA("winmm.dll");
    if (!hWinmm) {
        dbg("speedhack: winmm.dll introuvable");
        return false;
    }

    s_target = (BYTE*)GetProcAddress(hWinmm, "timeGetTime");
    if (!s_target) {
        dbg("speedhack: GetProcAddress(timeGetTime) echoue");
        return false;
    }

    memcpy(s_saved_bytes, s_target, 5);

    s_gateway = (BYTE*)VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!s_gateway) {
        dbg("speedhack: VirtualAlloc gateway echoue");
        return false;
    }

    memcpy(s_gateway, s_saved_bytes, 5);

    DWORD back = (DWORD)(s_target + 5 - (s_gateway + 5) - 5);
    s_gateway[5] = 0xE9;
    *(DWORD*)(s_gateway + 6) = back;

    s_real_timeGetTime = (PFN_timeGetTime)s_gateway;

    DWORD oldProt = 0;
    if (!VirtualProtect(s_target, 5, PAGE_EXECUTE_READWRITE, &oldProt)) {
        dbg("speedhack: VirtualProtect target echoue");
        return false;
    }

    DWORD rel = (DWORD)((BYTE*)&hook_timeGetTime_stub - s_target - 5);
    s_target[0] = 0xE9;
    *(DWORD*)(s_target + 1) = rel;

    VirtualProtect(s_target, 5, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), s_target, 5);

    EnterCriticalSection(&s_cs);
    reset_bases_locked();
    LeaveCriticalSection(&s_cs);

    s_hook_installed = true;
    dbg("speedhack: inline hook timeGetTime OK target=%p gateway=%p", s_target, s_gateway);
    return true;
}

static void apply_speedhack_state() {
    if (!install_inline_hook()) {
        dbg("speedhack: activation annulee, hook inline non installe");
        return;
    }

    EnterCriticalSection(&s_cs);
    reset_bases_locked();
    LeaveCriticalSection(&s_cs);

    dbg("speedhack: apply enabled=%d value10=%d factor=%.1f",
        g_speedhack_enabled ? 1 : 0,
        g_speedhack_value10,
        g_speedhack_enabled ? ((double)g_speedhack_value10 / 10.0) : 1.0);
}

void opt_speedhack_init(const char* ini_path) {
    lstrcpynA(g_ini_path, ini_path ? ini_path : "", MAX_PATH);

    g_speedhack_enabled = GetPrivateProfileIntA("speedhack", "enabled", 0, g_ini_path) ? true : false;
    g_speedhack_value10 = GetPrivateProfileIntA("speedhack", "value10", 10, g_ini_path);

    if (g_speedhack_value10 < 0) g_speedhack_value10 = 0;
    if (g_speedhack_value10 > 100) g_speedhack_value10 = 100;
}

void opt_speedhack_set_hwnd_and_start(HWND) {
    dbg("speedhack: startup ok, aucun hook installe au boot");
}

void opt_speedhack_toggle(bool enabled) {
    g_speedhack_enabled = enabled;
    save_cfg();
    apply_speedhack_state();
}

void opt_speedhack_apply(int value10) {
    if (value10 < 0) value10 = 0;
    if (value10 > 100) value10 = 100;

    g_speedhack_value10 = value10;
    save_cfg();

    if (g_speedhack_enabled) {
        apply_speedhack_state();
    } else {
        dbg("speedhack: OFF sans hook");
    }
}