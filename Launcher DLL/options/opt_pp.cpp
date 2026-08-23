#include "opt_pp.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>
#include <string.h>

bool g_pp_lock = false;

static char          s_ini[MAX_PATH];
static volatile LONG s_enabled   = 0;
static volatile LONG s_pending   = 0;
static volatile LONG s_installed = 0;

static volatile LONG s_retry_started = 0;

static char s_ruby[8192];

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

// Installe les gardes au niveau de la reduction et de l'ecriture des PP.
// pbReducePP est le chemin normal d'utilisation d'une attaque : le court-
// circuiter empeche la decrementation avant qu'elle puisse etre affichee.
// pbSetPP couvre les autres reductions (Pressure, Spite, Grudge, etc.).
// Les trois gardes ne s'appliquent qu'aux combattants du joueur.
static void build_ruby_apply() {
    const char* enabled = InterlockedExchangeAdd(&s_enabled, 0) ? "true" : "false";
    ULONG_PTR installed_addr = (ULONG_PTR)&s_installed;

    _snprintf(
        s_ruby,
        sizeof(s_ruby) - 1,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_pp_lock=%s\n"
        "  if defined?(::PokeBattle_Battler)\n"
        "    pp_methods=PokeBattle_Battler.instance_methods.collect { |m| m.to_s }\n"
        "    if pp_methods.include?(\"pbSetPP\") && pp_methods.include?(\"pbReducePP\") && pp_methods.include?(\"pbReducePPOther\")\n"
        "    class ::PokeBattle_Battler\n"
        "      def __uranium_trainer_pp_owned_by_player?\n"
        "        return false if !@battle\n"
        "        return @battle.pbOwnedByPlayer?(@index) ? true : false\n"
        "      rescue Exception\n"
        "        return false\n"
        "      end\n"
        "      def __uranium_trainer_pp_maximum(move)\n"
        "        return 0 if !move\n"
        "        return (move.totalpp rescue 0).to_i\n"
        "      rescue Exception\n"
        "        return 0\n"
        "      end\n"
        "      def pbSetPP(move,pp)\n"
        "        if $__uranium_trainer_pp_lock && __uranium_trainer_pp_owned_by_player?\n"
        "          maximum=__uranium_trainer_pp_maximum(move)\n"
        "          pp=maximum if maximum>0\n"
        "          end\n"
        "        move.pp=pp\n"
        "        if move.thismove && move.id==move.thismove.id && !@effects[PBEffects::Transform]\n"
        "          move.thismove.pp=pp\n"
        "        end\n"
        "      end\n"
        "      def pbReducePP(move)\n"
        "        if $__uranium_trainer_pp_lock && __uranium_trainer_pp_owned_by_player? && __uranium_trainer_pp_maximum(move)>0\n"
        "          return true\n"
        "        end\n"
        "        if @effects[PBEffects::TwoTurnAttack]>0 ||\n"
        "           @effects[PBEffects::Bide]>0 ||\n"
        "           @effects[PBEffects::Outrage]>0 ||\n"
        "           @effects[PBEffects::Rollout]>0 ||\n"
        "           @effects[PBEffects::HyperBeam]>0 ||\n"
        "           @effects[PBEffects::Uproar]>0\n"
        "          return true\n"
        "        end\n"
        "        return true if move.pp<0\n"
        "        return true if move.totalpp==0\n"
        "        return false if move.pp==0\n"
        "        pbSetPP(move,move.pp-1) if move.pp>0\n"
        "        return true\n"
        "      end\n"
        "      def pbReducePPOther(move)\n"
        "        if $__uranium_trainer_pp_lock && __uranium_trainer_pp_owned_by_player? && __uranium_trainer_pp_maximum(move)>0\n"
        "          return true\n"
        "        end\n"
        "        pbSetPP(move,move.pp-1) if move.pp>0\n"
        "      end\n"
        "    end\n"
        "    installed=1\n"
        "    end\n"
        "  end\n"
        "  if $__uranium_trainer_pp_lock\n"
        "    if defined?($Trainer) && $Trainer && $Trainer.party\n"
        "      $Trainer.party.each do |pkmn|\n"
        "        next if !pkmn\n"
        "        moves=(pkmn.moves rescue nil)\n"
        "        next if !moves\n"
        "        moves.each do |move|\n"
        "          next if !move\n"
        "          begin\n"
        "            maximum=(move.totalpp rescue 0).to_i\n"
        "            move.pp=maximum if maximum>0\n"
        "          rescue Exception\n"
        "          end\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "    if defined?(PokeBattle_Battle)\n"
        "      ObjectSpace.each_object(PokeBattle_Battle) do |battle|\n"
        "        begin\n"
        "          battlers=(battle.battlers rescue nil)\n"
        "          next if !battlers\n"
        "          battlers.each do |battler|\n"
        "            next if !battler\n"
        "            next if !(battle.pbOwnedByPlayer?(battler.index) rescue false)\n"
        "            moves=(battler.moves rescue nil)\n"
        "            next if !moves\n"
        "            moves.each do |move|\n"
        "              next if !move\n"
        "              maximum=(move.totalpp rescue 0).to_i\n"
        "              battler.pbSetPP(move,maximum) if maximum>0\n"
        "            end\n"
        "          end\n"
        "        rescue Exception\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "rescue Exception\n"
        "end\n"
        "begin\n"
        "  writer=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  writer.call(%lu,[installed].pack(\"l\"),4)\n"
        "rescue Exception\n"
        "end\n",
        enabled,
        (unsigned long)installed_addr
    );
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_pending, 0) == 0) return;

    build_ruby_apply();
    if (rgss_safe_eval(s_ruby) != 0)
        InterlockedExchange(&s_pending, 1);
}

// En injection au demarrage, les classes Ruby peuvent ne pas encore exister.
// Ce petit retry s'arrete des que le wrapper a ete installe.
static DWORD WINAPI retry_thread_proc(LPVOID) {
    while (InterlockedExchangeAdd(&s_installed, 0) == 0 &&
           !rgss_safe_dispatch_is_stopping()) {
        InterlockedExchange(&s_pending, 1);
        post_to_game();
        Sleep(500);
    }
    InterlockedExchange(&s_retry_started, 0);
    return 0;
}

static void start_retry_thread() {
    if (InterlockedExchangeAdd(&s_installed, 0) != 0) return;
    if (InterlockedCompareExchange(&s_retry_started, 1, 0) != 0) return;

    HANDLE thread = CreateThread(NULL, 0, retry_thread_proc, NULL, 0, NULL);
    if (!thread) {
        InterlockedExchange(&s_retry_started, 0);
        return;
    }

    // Le thread ne requiert ni join ni attente : le handle utilisateur peut
    // etre ferme immediatement sans interrompre son execution.
    CloseHandle(thread);
}

void opt_pp_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_pp_lock = GetPrivateProfileIntA("Settings", "PpLock", 0, s_ini) != 0;
    InterlockedExchange(&s_enabled, g_pp_lock ? 1 : 0);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
}

void opt_pp_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    start_retry_thread();
}

void opt_pp_toggle(bool enabled) {
    g_pp_lock = enabled;
    InterlockedExchange(&s_enabled, enabled ? 1 : 0);
    WritePrivateProfileStringA("Settings", "PpLock", enabled ? "1" : "0", s_ini);

    // Applique immediatement l'etat Ruby. Lors de l'activation, cela remplit
    // aussi les PP du groupe et des battlers deja presents dans un combat.
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    start_retry_thread();
}
