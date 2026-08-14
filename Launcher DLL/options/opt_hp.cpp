#include "opt_hp.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

bool g_hp_lock = false;

static char          s_ini[MAX_PATH];
static volatile LONG s_enabled = 0;
static volatile LONG s_pending = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;
static char          s_ruby[8192];

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

// RGSS1 ne rend pas toutes les methodes de combat visibles a method_defined?
// sur cette build. On reinstalle donc en memoire les petits corps natifs de
// hp= et pbReduceHP avec un garde optionnel. Le chemin OFF est identique aux
// scripts d'origine et aucun fichier du jeu n'est modifie.
static void build_ruby_apply() {
    const char* enabled =
        InterlockedExchangeAdd(&s_enabled, 0) ? "true" : "false";
    _snprintf_s(
        s_ruby, sizeof(s_ruby), _TRUNCATE,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_hp_lock=%s\n"
        "  if defined?(PokeBattle_Battler) && defined?(PokeBattle_Pokemon)\n"
        "    class PokeBattle_Battler\n"
        "      def hp=(value)\n"
        "        owned=false\n"
        "        begin\n"
        "          owned=$__uranium_trainer_hp_lock && @battle && @pokemon && @battle.pbOwnedByPlayer?(@index)\n"
        "        rescue Exception\n"
        "          owned=false\n"
        "        end\n"
        "        return @hp if owned && @hp && value.to_i<@hp.to_i\n"
        "        @hp=value.to_i\n"
        "        @pokemon.hp=value.to_i if @pokemon\n"
        "      end\n"
        "      def pbReduceHP(amt,anim=false)\n"
        "        begin\n"
        "          return 0 if $__uranium_trainer_hp_lock && @battle && @pokemon && @battle.pbOwnedByPlayer?(@index)\n"
        "        rescue Exception\n"
        "        end\n"
        "        if amt>=self.hp\n"
        "          amt=self.hp\n"
        "        elsif amt<=0 && !self.isFainted?\n"
        "          amt=1\n"
        "        end\n"
        "        oldhp=self.hp\n"
        "        self.hp-=amt\n"
        "        raise _INTL(\"HP less than 0\") if self.hp<0\n"
        "        raise _INTL(\"HP greater than total HP\") if self.hp>@totalhp\n"
        "        @battle.scene.pbHPChanged(self,oldhp,anim) if amt>0\n"
        "        return amt\n"
        "      end\n"
        "    end\n"
        "    class PokeBattle_Pokemon\n"
        "      def hp=(value)\n"
        "        owned=false\n"
        "        begin\n"
        "          if $__uranium_trainer_hp_lock && $Trainer && $Trainer.party\n"
        "            $Trainer.party.each do |pkmn|\n"
        "              if pkmn.equal?(self)\n"
        "                owned=true\n"
        "                break\n"
        "              end\n"
        "            end\n"
        "          end\n"
        "        rescue Exception\n"
        "          owned=false\n"
        "        end\n"
        "        return @hp if owned && @hp && value.to_i<@hp.to_i\n"
        "        value=0 if value<0\n"
        "        @hp=value\n"
        "        if @hp==0\n"
        "          @status=0\n"
        "          @statusCount=0\n"
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
    build_ruby_apply();
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

void opt_hp_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_hp_lock =
        GetPrivateProfileIntA("Settings", "HpLock", 0, s_ini) != 0;
    InterlockedExchange(&s_enabled, g_hp_lock ? 1 : 0);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
}

void opt_hp_set_hwnd(HWND hwnd) {
    (void)hwnd;
}

void opt_hp_set_hwnd_and_start(HWND hwnd) {
    opt_hp_set_hwnd(hwnd);
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}

void opt_hp_toggle(bool enabled) {
    g_hp_lock = enabled;
    InterlockedExchange(&s_enabled, enabled ? 1 : 0);
    WritePrivateProfileStringA("Settings", "HpLock",
                               enabled ? "1" : "0", s_ini);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}
