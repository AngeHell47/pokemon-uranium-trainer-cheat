#include "opt_ohk.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

// KO en un coup est applique au point central de retrait des PV. Le hook ne
// touche qu'aux cibles qui n'appartiennent pas au joueur, meme en double.
bool g_ohk_lock = false;

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
        "  $__uranium_trainer_ohk=%s\n"
        "  if defined?(::PokeBattle_Move)\n"
        "    class ::PokeBattle_Move\n"
        "      unless instance_methods.collect { |m| m.to_s }.include?(\"__uranium_trainer_original_pbReduceHPDamage_ohk\")\n"
        "        alias_method :__uranium_trainer_original_pbReduceHPDamage_ohk, :pbReduceHPDamage\n"
        "      end\n"
        "      def pbReduceHPDamage(damage,attacker,opponent)\n"
        "        begin\n"
        "          enemy=opponent && @battle && !@battle.pbOwnedByPlayer?(opponent.index)\n"
        "          if $__uranium_trainer_ohk && enemy && damage.to_i>0\n"
        "            # La methode originale garde les effets et scripts du combat.\n"
        "            damage=(opponent.hp rescue damage).to_i\n"
        "            damage=1 if damage<1\n"
        "          end\n"
        "        rescue Exception\n"
        "        end\n"
        "        __uranium_trainer_original_pbReduceHPDamage_ohk(damage,attacker,opponent)\n"
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

void opt_ohk_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_ohk_lock = GetPrivateProfileIntA("Settings", "OhkLock", 0, s_ini) != 0;
    InterlockedExchange(&s_enabled, g_ohk_lock ? 1 : 0);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
}

void opt_ohk_set_hwnd_and_start(HWND) {
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    InterlockedExchange(&s_pending, 1);
    notify_game();
    ensure_retry_thread();
}

void opt_ohk_toggle(bool enabled) {
    g_ohk_lock = enabled;
    InterlockedExchange(&s_enabled, enabled ? 1 : 0);
    WritePrivateProfileStringA("Settings", "OhkLock", enabled ? "1" : "0", s_ini);
    InterlockedExchange(&s_pending, 1);
    notify_game();
    ensure_retry_thread();
}
