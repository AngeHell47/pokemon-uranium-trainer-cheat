#include "opt_noenc.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

bool g_noenc = false;

static char          s_ini[MAX_PATH];
static volatile LONG s_enabled = 0;
static volatile LONG s_pending = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;
static char s_ruby[4096];

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

static void build_ruby() {
    const char* enabled = InterlockedExchangeAdd(&s_enabled, 0) ? "true" : "false";
    _snprintf(
        s_ruby, sizeof(s_ruby) - 1,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_noenc=%s\n"
        "  if defined?($game_system) && $game_system\n"
        "    $game_system.encounter_disabled=($__uranium_trainer_noenc ? true : false)\n"
        "  end\n"
        "  save_guard=0\n"
        "  if Object.private_method_defined?(:pbSave) || Object.method_defined?(:pbSave)\n"
        "    class Object\n"
        "      unless private_method_defined?(:__uranium_trainer_original_pbSave_noenc)\n"
        "        alias_method :__uranium_trainer_original_pbSave_noenc, :pbSave\n"
        "        def pbSave(*args)\n"
        "          protect=$__uranium_trainer_noenc && $game_system\n"
        "          previous=$game_system.encounter_disabled if protect\n"
        "          $game_system.encounter_disabled=false if protect\n"
        "          begin\n"
        "            __uranium_trainer_original_pbSave_noenc(*args)\n"
        "          ensure\n"
        "            $game_system.encounter_disabled=previous if protect && $game_system\n"
        "          end\n"
        "        end\n"
        "        private :pbSave\n"
        "      end\n"
        "    end\n"
        "    save_guard=1 if Object.private_method_defined?(:__uranium_trainer_original_pbSave_noenc)\n"
        "  end\n"
        "  installed=1 if save_guard==1 && defined?($game_system) && $game_system\n"
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
    while (InterlockedExchangeAdd(&s_installed, 0) == 0 &&
           !rgss_safe_dispatch_is_stopping()) {
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
}

void opt_noenc_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
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
