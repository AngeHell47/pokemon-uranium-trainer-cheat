#include "opt_itemlock.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

// Toutes les consommations du sac passent par pbDeleteItem. Les objets-clefs
// (poche 2) restent supprimables pour ne pas bloquer les scripts du scenario.
bool g_item_lock = false;

static char s_ini[MAX_PATH] = {};
static volatile LONG s_enabled = 0;
static volatile LONG s_pending = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;
static char s_ruby[4096] = {};

static void notify_game() { rgss_safe_dispatch_notify(); }

static void build_ruby() {
    const char* enabled = InterlockedExchangeAdd(&s_enabled, 0) ? "true" : "false";
    _snprintf_s(s_ruby, sizeof(s_ruby), _TRUNCATE,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_item_lock=%s\n"
        "  if defined?(::PokemonBag)\n"
        "    class ::PokemonBag\n"
        "      unless instance_methods.collect { |m| m.to_s }.include?(\"__uranium_trainer_original_pbDeleteItem\")\n"
        "        alias_method :__uranium_trainer_original_pbDeleteItem, :pbDeleteItem\n"
        "      end\n"
        "      def pbDeleteItem(item,qty=1)\n"
        "        begin\n"
        "          pocket=(pbGetPocket(item) rescue -1).to_i\n"
        "          return true if $__uranium_trainer_item_lock && pocket!=2 && qty.to_i>0\n"
        "        rescue Exception\n"
        "        end\n"
        "        __uranium_trainer_original_pbDeleteItem(item,qty)\n"
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
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_pending, 0) == 0) return;
    build_ruby();
    if (rgss_safe_eval(s_ruby) != 0) InterlockedExchange(&s_pending, 1);
}

static DWORD WINAPI retry_thread(LPVOID) {
    while (InterlockedExchangeAdd(&s_installed, 0) == 0 &&
           !rgss_safe_dispatch_is_stopping()) {
        InterlockedExchange(&s_pending, 1);
        notify_game();
        Sleep(500);
    }
    InterlockedExchange(&s_retry_started, 0);
    return 0;
}

static void ensure_retry_thread() {
    if (InterlockedExchangeAdd(&s_installed, 0) != 0 ||
        InterlockedCompareExchange(&s_retry_started, 1, 0) != 0) return;
    HANDLE thread = CreateThread(NULL, 0, retry_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    else InterlockedExchange(&s_retry_started, 0);
}

void opt_itemlock_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_item_lock = GetPrivateProfileIntA("Settings", "ItemLock", 0, s_ini) != 0;
    InterlockedExchange(&s_enabled, g_item_lock ? 1 : 0);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
}

void opt_itemlock_set_hwnd_and_start(HWND) {
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    InterlockedExchange(&s_pending, 1);
    notify_game();
    ensure_retry_thread();
}

void opt_itemlock_toggle(bool enabled) {
    g_item_lock = enabled;
    InterlockedExchange(&s_enabled, enabled ? 1 : 0);
    WritePrivateProfileStringA("Settings", "ItemLock", enabled ? "1" : "0", s_ini);
    InterlockedExchange(&s_pending, 1);
    notify_game();
    ensure_retry_thread();
}
