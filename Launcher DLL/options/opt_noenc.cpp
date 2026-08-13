#include "opt_noenc.h"
#include "../trainer_runtime.h"

#include <stdio.h>

bool g_noenc = false;

static char          s_ini[MAX_PATH];
static HWND          s_game_hwnd = NULL;
static DWORD         s_game_tid = 0;
static volatile LONG s_enabled = 0;
static volatile LONG s_pending = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;
static HHOOK         s_hook_cwp = NULL;
static HHOOK         s_hook_getmsg = NULL;

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;
static char s_ruby[4096];

static bool resolve() {
    if (s_eval) return true;
    HMODULE rgss = GetModuleHandleA("RGSS102E.dll");
    if (!rgss) return false;
    s_eval = (RGSSEval_t)GetProcAddress(rgss, "RGSSEval");
    return s_eval != NULL;
}

static void post_to_game() {
    if (s_game_hwnd) PostMessageA(s_game_hwnd, WM_NULL, 0, 0);
    if (s_game_tid) PostThreadMessageA(s_game_tid, WM_NULL, 0, 0);
}

static void build_ruby() {
    const char* enabled = InterlockedExchangeAdd(&s_enabled, 0) ? "true" : "false";
    _snprintf(
        s_ruby, sizeof(s_ruby) - 1,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_noenc=%s\n"
        "  if defined?(PokemonEncounters) && PokemonEncounters.method_defined?(:pbCanEncounter?)\n"
        "    class PokemonEncounters\n"
        "      unless method_defined?(:__uranium_trainer_original_can_encounter)\n"
        "        alias_method :__uranium_trainer_original_can_encounter, :pbCanEncounter?\n"
        "        def pbCanEncounter?(encounter)\n"
        "          return false if $__uranium_trainer_noenc\n"
        "          __uranium_trainer_original_can_encounter(encounter)\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "    installed=1\n"
        "  end\n"
        "rescue Exception\n"
        "end\n"
        "begin\n"
        "  Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,[installed].pack(\"l\"),4)\n"
        "rescue Exception\n"
        "end\n",
        enabled, (unsigned long)(ULONG_PTR)&s_installed);
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

static void on_game_thread_tick() {
    if (InterlockedExchange(&s_pending, 0) == 0) return;
    if (!resolve()) {
        InterlockedExchange(&s_pending, 1);
        return;
    }
    build_ruby();
    s_eval(s_ruby);
}

static LRESULT CALLBACK cwp_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) on_game_thread_tick();
    return CallNextHookEx(s_hook_cwp, code, wp, lp);
}

static LRESULT CALLBACK getmsg_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) on_game_thread_tick();
    return CallNextHookEx(s_hook_getmsg, code, wp, lp);
}

static void install_hooks() {
    if (!s_game_tid || !g_trainer_module) return;
    if (!s_hook_cwp)
        s_hook_cwp = SetWindowsHookExA(WH_CALLWNDPROC, cwp_hook, g_trainer_module, s_game_tid);
    if (!s_hook_getmsg)
        s_hook_getmsg = SetWindowsHookExA(WH_GETMESSAGE, getmsg_hook, g_trainer_module, s_game_tid);
}

static DWORD WINAPI retry_thread(LPVOID) {
    while (InterlockedExchangeAdd(&s_installed, 0) == 0) {
        InterlockedExchange(&s_pending, 1);
        post_to_game();
        Sleep(500);
    }
    InterlockedExchange(&s_retry_started, 0);
    return 0;
}

static void ensure_retry_thread() {
    if (InterlockedExchangeAdd(&s_installed, 0) != 0) return;
    if (InterlockedCompareExchange(&s_retry_started, 1, 0) != 0) return;
    HANDLE thread = CreateThread(NULL, 0, retry_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    else InterlockedExchange(&s_retry_started, 0);
}

void opt_noenc_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_noenc = GetPrivateProfileIntA("Settings", "NoEnc", 0, s_ini) != 0;
    InterlockedExchange(&s_enabled, g_noenc ? 1 : 0);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
    resolve();
}

void opt_noenc_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    s_game_tid = hwnd ? GetWindowThreadProcessId(hwnd, NULL) : 0;
    install_hooks();
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}

void opt_noenc_toggle(bool enabled) {
    g_noenc = enabled;
    InterlockedExchange(&s_enabled, enabled ? 1 : 0);
    WritePrivateProfileStringA("Settings", "NoEnc", enabled ? "1" : "0", s_ini);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}
