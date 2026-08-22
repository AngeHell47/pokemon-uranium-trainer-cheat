#include "opt_godmode_repair.h"
#include "opt_hp.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

namespace {

static volatile LONG s_pending = 0;
static LONG s_refresh_frames = 0;
static char s_ruby[8192] = {};

static void post_to_game() { rgss_safe_dispatch_notify(); }

// Tous les chemins offensifs d'Uranium finissent soit dans pbReduceHP,
// soit dans PokeBattle_Move#pbReduceHPDamage. Conserver les implementations
// originales par alias (au lieu d'en recopier le corps) couvre les ajouts du
// jeu et les combats doubles sans casser les animations ni les effets.
static void build_ruby() {
    const char* enabled = g_hp_lock ? "true" : "false";
    _snprintf_s(s_ruby, sizeof(s_ruby), _TRUNCATE,
        "begin\n"
        "  $__uranium_trainer_hp_lock=%s\n"
        "  if defined?(PokeBattle_Battler)\n"
        "    class PokeBattle_Battler\n"
        "      unless instance_methods.collect { |m| m.to_s }.include?(\"__uranium_trainer_repair_original_pbReduceHP\")\n"
        "        alias_method :__uranium_trainer_repair_original_pbReduceHP, :pbReduceHP\n"
        "      end\n"
        "      def __uranium_trainer_player_battler?\n"
        // Ne pas se baser uniquement sur l'index de combat : certains
        // combats scripts remappent les indices. La reference du Pokemon de
        // la team du joueur reste en revanche la meme pendant le combat.
        "        return false if !@pokemon || !$Trainer || !$Trainer.party\n"
        "        $Trainer.party.each { |pkmn| return true if pkmn && pkmn.equal?(@pokemon) }\n"
        "        false\n"
        "      rescue Exception\n"
        "        false\n"
        "      end\n"
        "      def pbReduceHP(amount,animation=false)\n"
        "        return 0 if $__uranium_trainer_hp_lock && __uranium_trainer_player_battler?\n"
        "        __uranium_trainer_repair_original_pbReduceHP(amount,animation)\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  if defined?(PokeBattle_Move)\n"
        "    class PokeBattle_Move\n"
        "      unless instance_methods.collect { |m| m.to_s }.include?(\"__uranium_trainer_repair_original_pbReduceHPDamage\")\n"
        "        alias_method :__uranium_trainer_repair_original_pbReduceHPDamage, :pbReduceHPDamage\n"
        "      end\n"
        "      def pbReduceHPDamage(damage,attacker,opponent)\n"
        "        begin\n"
        "          if $__uranium_trainer_hp_lock && opponent && opponent.__uranium_trainer_player_battler?\n"
        "            opponent.damagestate.calcdamage=0\n"
        "            opponent.damagestate.hplost=0\n"
        "            opponent.damagestate.substitute=false\n"
        "            return 0\n"
        "          end\n"
        "        rescue Exception\n"
        "        end\n"
        "        __uranium_trainer_repair_original_pbReduceHPDamage(damage,attacker,opponent)\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "rescue Exception\n"
        "end\n", enabled);
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_pending, 0) == 0) {
        // opt_hp repose ses wrappers toutes les cinq frames quand il est
        // actif. Repasser juste apres lui garantit que ce garde final reste
        // celui qui est execute, sans evaluer a chaque image.
        if (++s_refresh_frames < 5) return;
    }
    s_refresh_frames = 0;
    build_ruby();
    if (rgss_safe_eval(s_ruby) != 0) InterlockedExchange(&s_pending, 1);
}

} // namespace

bool opt_godmode_repair_init() {
    InterlockedExchange(&s_pending, 1);
    s_refresh_frames = 0;
    if (!rgss_safe_dispatch_register(on_game_thread_tick, NULL)) return false;
    post_to_game();
    return true;
}

void opt_godmode_repair_shutdown() {
    rgss_safe_dispatch_unregister(on_game_thread_tick, NULL);
}
