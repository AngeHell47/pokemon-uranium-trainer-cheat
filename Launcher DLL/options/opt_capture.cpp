#include "opt_capture.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

bool g_capture_guaranteed = false;

static char          s_ini[MAX_PATH];
static volatile LONG s_enabled = 0;
static volatile LONG s_pending = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;
static char          s_ruby[4096];

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

// Le calcul de capture d'Uranium consulte BallHandlers.isUnconditional? juste
// avant les quatre tests aleatoires. Le garde conserve les refus places plus
// haut par le jeu : combat de Dresseur, cible KO et rarete nulle restent donc
// intacts, comme avec une Master Ball normale.
static void build_ruby() {
    const char* enabled =
        InterlockedExchangeAdd(&s_enabled, 0) ? "true" : "false";
    _snprintf_s(
        s_ruby, sizeof(s_ruby), _TRUNCATE,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_capture_guaranteed=%s\n"
        "  if defined?(BallHandlers) && BallHandlers.respond_to?(:isUnconditional?)\n"
        "    class << BallHandlers\n"
        "      unless method_defined?(:__uranium_trainer_original_isUnconditional)\n"
        "        alias_method :__uranium_trainer_original_isUnconditional, :isUnconditional?\n"
        "        def isUnconditional?(ball,battle,battler)\n"
        "          return true if $__uranium_trainer_capture_guaranteed\n"
        "          __uranium_trainer_original_isUnconditional(ball,battle,battler)\n"
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

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_pending, 0) == 0) return;
    build_ruby();
    if (rgss_safe_eval(s_ruby) != 0)
        InterlockedExchange(&s_pending, 1);
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

void opt_capture_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_capture_guaranteed =
        GetPrivateProfileIntA("Settings", "CaptureGuaranteed", 0, s_ini) != 0;
    InterlockedExchange(&s_enabled, g_capture_guaranteed ? 1 : 0);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
}

void opt_capture_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}

void opt_capture_toggle(bool enabled) {
    g_capture_guaranteed = enabled;
    InterlockedExchange(&s_enabled, enabled ? 1 : 0);
    WritePrivateProfileStringA(
        "Settings", "CaptureGuaranteed", enabled ? "1" : "0", s_ini);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}
