#include "opt_hp.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

bool g_hp_lock = false;

static char          s_ini[MAX_PATH];
static volatile LONG s_enabled = 0;
static volatile LONG s_pending = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;
static volatile LONG s_runtime_state = 0;
static LONG          s_refresh_frames = 0;
static char          s_ruby[12288];

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

// Reproduit en memoire les trois points que le patch Scripts.rxdata validait :
// les deux ecrivains de HP et le chemin central des degats d'une attaque.
// L'etat 1/2/3 permet a l'overlay de distinguer installation, passage dans le
// hook et degat effectivement bloque.
static void build_ruby_apply() {
    const char* enabled =
        InterlockedExchangeAdd(&s_enabled, 0) ? "true" : "false";
    _snprintf_s(
        s_ruby, sizeof(s_ruby), _TRUNCATE,
        "installed=0\n"
        "begin\n"
        "  hp_enabled=%s\n"
        "  $__uranium_trainer_hp_lock=hp_enabled\n"
        "  $__uranium_trainer_hp_runtime_address=%lu\n"
        "  if $__uranium_trainer_hp_runtime_enabled!=hp_enabled\n"
        "    $__uranium_trainer_hp_runtime_enabled=hp_enabled\n"
        "    $__uranium_trainer_hp_runtime_state=0\n"
        "  end\n"
        "  $__uranium_trainer_hp_runtime_state||=0\n"
        "  $__uranium_trainer_hp_writer||=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  if defined?(::PokeBattle_Battler) && defined?(::PokeBattle_Pokemon)\n"
        "    class ::PokeBattle_Battler\n"
        "      def hp=(value)\n"
        "        $__uranium_trainer_hp_runtime_state=2 if %s && $__uranium_trainer_hp_runtime_state.to_i<2\n"
        "        owned=false\n"
        "        begin\n"
        "          owned=%s && @battle && @pokemon && @battle.pbOwnedByPlayer?(@index)\n"
        "        rescue Exception\n"
        "          owned=false\n"
        "        end\n"
        "        if owned && @hp && value.to_i<@hp.to_i\n"
        "          $__uranium_trainer_hp_runtime_state=3\n"
        "          return @hp\n"
        "        end\n"
        "        @hp=value.to_i\n"
        "        @pokemon.hp=value.to_i if @pokemon\n"
        "      end\n"
        "      def pbReduceHP(amt,anim=false)\n"
        "        $__uranium_trainer_hp_runtime_state=2 if %s && $__uranium_trainer_hp_runtime_state.to_i<2\n"
        "        begin\n"
        "          if %s && @battle && @pokemon && @battle.pbOwnedByPlayer?(@index)\n"
        "            $__uranium_trainer_hp_runtime_state=3\n"
        "            return 0\n"
        "          end\n"
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
        "    class ::PokeBattle_Pokemon\n"
        "      def hp=(value)\n"
        "        $__uranium_trainer_hp_runtime_state=2 if %s && $__uranium_trainer_hp_runtime_state.to_i<2\n"
        "        owned=false\n"
        "        begin\n"
        "          if %s && $Trainer && $Trainer.party\n"
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
        "        if owned && @hp && value.to_i<@hp.to_i\n"
        "          $__uranium_trainer_hp_runtime_state=3\n"
        "          return @hp\n"
        "        end\n"
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
        "  if defined?(::PokeBattle_Move)\n"
        "    class ::PokeBattle_Move\n"
        "      unless instance_methods.collect { |m| m.to_s }.include?(\"__uranium_trainer_original_pbReduceHPDamage_v5\")\n"
        "        alias_method :__uranium_trainer_original_pbReduceHPDamage_v5, :pbReduceHPDamage\n"
        "      end\n"
        "      def pbReduceHPDamage(damage,attacker,opponent)\n"
        "        $__uranium_trainer_hp_runtime_state=2 if %s && $__uranium_trainer_hp_runtime_state.to_i<2\n"
        "        begin\n"
        "          if %s && opponent && @battle && @battle.pbOwnedByPlayer?(opponent.index)\n"
        "            $__uranium_trainer_hp_runtime_state=3\n"
        "            opponent.damagestate.calcdamage=0\n"
        "            opponent.damagestate.hplost=0\n"
        "            opponent.damagestate.substitute=false\n"
        "            return 0\n"
        "          end\n"
        "        rescue Exception\n"
        "        end\n"
        "        return __uranium_trainer_original_pbReduceHPDamage_v5(damage,attacker,opponent)\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  $__uranium_trainer_hp_runtime_state=1 if installed==1 && $__uranium_trainer_hp_runtime_state.to_i<1\n"
        "rescue Exception\n"
        "end\n"
        "begin\n"
        "  final_writer=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  final_writer.call(%lu,[installed].pack(\"l\"),4)\n"
        "  final_writer.call(%lu,[$__uranium_trainer_hp_runtime_state.to_i].pack(\"l\"),4)\n"
        "rescue Exception\n"
        "end\n",
        enabled, (unsigned long)(ULONG_PTR)&s_runtime_state,
        enabled, enabled, enabled, enabled, enabled, enabled, enabled, enabled,
        (unsigned long)(ULONG_PTR)&s_installed,
        (unsigned long)(ULONG_PTR)&s_runtime_state);
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_pending, 0) == 0) {
        // Uranium peut recharger tardivement ses classes de combat. Reposer
        // les trois petits corps une fois par seconde rend le hook resistant
        // a cet ecrasement sans aucune ecriture disque.
        const LONG interval =
            InterlockedExchangeAdd(&s_enabled, 0) ? 5 : 60;
        if (++s_refresh_frames < interval) return;
    }
    s_refresh_frames = 0;
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
    InterlockedExchange(&s_runtime_state, 0);
    s_refresh_frames = 0;
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
    InterlockedExchange(&s_runtime_state, 0);
    WritePrivateProfileStringA("Settings", "HpLock",
                               enabled ? "1" : "0", s_ini);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}

int opt_hp_runtime_state() {
    if (InterlockedExchangeAdd(&s_installed, 0) == 0) return 0;
    return (int)InterlockedExchangeAdd(&s_runtime_state, 0);
}
