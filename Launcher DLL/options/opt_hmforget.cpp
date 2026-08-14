#include "opt_hmforget.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

bool g_hm_forget_enabled = false;

static char          s_ini[MAX_PATH];
static volatile LONG s_enabled = 0;
static volatile LONG s_pending = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;
static char          s_ruby[4096];

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

// Reinstalle en memoire le petit corps natif de pbIsHiddenMove? avec un garde
// optionnel. Ce point unique est appele par l'ecran de remplacement et ne
// change ni les objets HM ni l'utilisation des CS sur la carte.
static void build_ruby() {
    const char* enabled =
        InterlockedExchangeAdd(&s_enabled, 0) ? "true" : "false";
    _snprintf_s(
        s_ruby, sizeof(s_ruby), _TRUNCATE,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_hm_forget=%s\n"
        "  class Object\n"
        "    def pbIsHiddenMove?(move)\n"
        "      return false if $__uranium_trainer_hm_forget\n"
        "      return false if !$ItemData\n"
        "      for i in 0...$ItemData.length\n"
        "        next if !pbIsHiddenMachine?(i)\n"
        "        atk=$ItemData[i][ITEMMACHINE]\n"
        "        return true if move==atk\n"
        "      end\n"
        "      return false\n"
        "    end\n"
        "    private :pbIsHiddenMove?\n"
        "  end\n"
        "  installed=1\n"
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

void opt_hmforget_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_hm_forget_enabled =
        GetPrivateProfileIntA("Settings", "HmForgetEnabled", 0, s_ini) != 0;
    InterlockedExchange(&s_enabled, g_hm_forget_enabled ? 1 : 0);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
}

void opt_hmforget_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}

void opt_hmforget_toggle(bool enabled) {
    g_hm_forget_enabled = enabled;
    InterlockedExchange(&s_enabled, enabled ? 1 : 0);
    WritePrivateProfileStringA(
        "Settings", "HmForgetEnabled", enabled ? "1" : "0", s_ini);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}
