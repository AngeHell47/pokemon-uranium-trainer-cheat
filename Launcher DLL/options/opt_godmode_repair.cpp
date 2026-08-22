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
        "      unless instance_methods.collect { |m| m.to_s }.include?(\"__uranium_trainer_repair_original_hp\")\n"
        "        alias_method :__uranium_trainer_repair_original_hp, :hp\n"
        "      end\n"
        "      unless instance_methods.collect { |m| m.to_s }.include?(\"__uranium_trainer_repair_original_pbReduceHP\")\n"
        "        alias_method :__uranium_trainer_repair_original_pbReduceHP, :pbReduceHP\n"
        "      end\n"
        "      unless instance_methods.collect { |m| m.to_s }.include?(\"__uranium_trainer_repair_original_isFainted\")\n"
        "        alias_method :__uranium_trainer_repair_original_isFainted, :isFainted?\n"
        "      end\n"
        "      def __uranium_trainer_player_battler?\n"
        // Ne pas se baser uniquement sur l'index de combat : certains
        // combats scripts remappent les indices. La reference du Pokemon de
        // la team du joueur reste en revanche la meme pendant le combat.
        "        if $Trainer && $Trainer.party && @pokemon\n"
        "          $Trainer.party.each { |pkmn| return true if pkmn && pkmn.equal?(@pokemon) }\n"
        "        end\n"
        "        return @battle.pbOwnedByPlayer?(@index) if @battle && @battle.respond_to?(:pbOwnedByPlayer?)\n"
        "        false\n"
        "      rescue Exception\n"
        "        false\n"
        "      end\n"
        // Les scripts de certaines attaques Uranium peuvent modifier @hp sans
        // passer par pbReduceHP. Le lecteur hp est commun aux calculs, aux
        // animations et au test de K.O. Restaurer ici les PV du combattant du
        // joueur couvre ces chemins speciaux sans toucher aux adversaires.
        "      def hp\n"
        "        if $__uranium_trainer_hp_lock && __uranium_trainer_player_battler? && @totalhp\n"
        "          @hp=@totalhp\n"
        "          @pokemon.hp=@pokemon.totalhp if @pokemon && @pokemon.respond_to?(:totalhp)\n"
        "        end\n"
        "        @hp\n"
        "      rescue Exception\n"
        "        __uranium_trainer_repair_original_hp\n"
        "      end\n"
        "      def pbReduceHP(amount,animation=false)\n"
        "        return 0 if $__uranium_trainer_hp_lock && __uranium_trainer_player_battler?\n"
        "        __uranium_trainer_repair_original_pbReduceHP(amount,animation)\n"
        "      end\n"
        "      def isFainted?\n"
        "        return false if $__uranium_trainer_hp_lock && __uranium_trainer_player_battler?\n"
        "        __uranium_trainer_repair_original_isFainted\n"
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
        // Filet final : quelques scripts Uranium ecrivent directement les
        // variables @hp. Synchroniser les objets de l'equipe et les battlers
        // actifs toutes les quelques frames rend ces ecritures inoffensives,
        // y compris pour les degats de terrain et les attaques speciales.
        "  if $__uranium_trainer_hp_lock && defined?($Trainer) && $Trainer && $Trainer.party\n"
        "    $Trainer.party.each do |pkmn|\n"
        "      pkmn.instance_variable_set(:@hp,pkmn.totalhp) if pkmn && pkmn.respond_to?(:totalhp)\n"
        "    end\n"
        "    if defined?(PokeBattle_Battler)\n"
        "      ObjectSpace.each_object(PokeBattle_Battler) do |battler|\n"
        "        if battler.__uranium_trainer_player_battler? && battler.respond_to?(:totalhp)\n"
        "          battler.instance_variable_set(:@hp,battler.totalhp)\n"
        "          pkmn=(battler.pokemon rescue nil)\n"
        "          pkmn.instance_variable_set(:@hp,pkmn.totalhp) if pkmn && pkmn.respond_to?(:totalhp)\n"
        "        end\n"
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
