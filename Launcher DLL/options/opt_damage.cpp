#include "opt_damage.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

// Multiplie seulement les degats effectivement infliges par le joueur aux
// ennemis. Les degats de recul, confusion et allies ne sont pas concernes.
int g_damage_multiplier = 1;

static char s_ini[MAX_PATH] = {};
static volatile LONG s_multiplier = 1;
static volatile LONG s_pending = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;
static char s_ruby[4096] = {};

static void notify_game() { rgss_safe_dispatch_notify(); }

static void build_ruby() {
    int multiplier = (int)InterlockedExchangeAdd(&s_multiplier, 0);
    if (multiplier < 1) multiplier = 1;
    if (multiplier > 100) multiplier = 100;
    _snprintf_s(s_ruby, sizeof(s_ruby), _TRUNCATE,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_damage_multiplier=%d\n"
        "  if defined?(::PokeBattle_Move)\n"
        "    class ::PokeBattle_Move\n"
        "      unless instance_methods.collect { |m| m.to_s }.include?(\"__uranium_trainer_original_pbReduceHPDamage_multiplier\")\n"
        "        alias_method :__uranium_trainer_original_pbReduceHPDamage_multiplier, :pbReduceHPDamage\n"
        "      end\n"
        "      def pbReduceHPDamage(damage,attacker,opponent)\n"
        "        begin\n"
        "          player_hit=attacker && opponent && @battle &&\n"
        "            @battle.pbOwnedByPlayer?(attacker.index) && !@battle.pbOwnedByPlayer?(opponent.index)\n"
        "          if player_hit && damage.to_i>0 && $__uranium_trainer_damage_multiplier.to_i>1\n"
        "            damage=damage.to_i*$__uranium_trainer_damage_multiplier.to_i\n"
        "          end\n"
        "        rescue Exception\n"
        "        end\n"
        "        __uranium_trainer_original_pbReduceHPDamage_multiplier(damage,attacker,opponent)\n"
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
        multiplier, (unsigned long)(ULONG_PTR)&s_installed);
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

void opt_damage_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_damage_multiplier = GetPrivateProfileIntA("Settings", "DamageMultiplier", 1, s_ini);
    if (g_damage_multiplier < 1 || g_damage_multiplier > 100) g_damage_multiplier = 1;
    InterlockedExchange(&s_multiplier, g_damage_multiplier);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
}

void opt_damage_set_hwnd_and_start(HWND) {
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    InterlockedExchange(&s_pending, 1);
    notify_game();
    ensure_retry_thread();
}

void opt_damage_apply(int multiplier) {
    if (multiplier < 1) multiplier = 1;
    if (multiplier > 100) multiplier = 100;
    g_damage_multiplier = multiplier;
    InterlockedExchange(&s_multiplier, multiplier);
    char value[16] = {};
    wsprintfA(value, "%d", multiplier);
    WritePrivateProfileStringA("Settings", "DamageMultiplier", value, s_ini);
    InterlockedExchange(&s_pending, 1);
    notify_game();
    ensure_retry_thread();
}
