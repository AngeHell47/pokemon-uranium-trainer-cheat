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

// Installe une seule fois un garde autour de PokeBattle_Battler#pbSetPP.
// Dans cette version d'Uranium, toutes les baisses de PP passent par cette
// methode : utilisation normale, Pressure, Grudge et Spite. Le garde utilise
// pbOwnedByPlayer? afin de ne jamais modifier les PP adverses.
static void build_ruby_apply() {
    const char* enabled = InterlockedExchangeAdd(&s_enabled, 0) ? "true" : "false";
    ULONG_PTR installed_addr = (ULONG_PTR)&s_installed;

    _snprintf(
        s_ruby,
        sizeof(s_ruby) - 1,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_pp_lock=%s\n"
        "  if defined?(PokeBattle_Battler)\n"
        "    class PokeBattle_Battler\n"
        "      def pbSetPP(move,pp)\n"
        "          if $__uranium_trainer_pp_lock && move\n"
        "            owned=false\n"
        "            begin\n"
        "              owned=(@battle && @battle.pbOwnedByPlayer?(@index)) ? true : false\n"
        "            rescue Exception\n"
        "              owned=false\n"
        "            end\n"
        "            if owned\n"
        "              begin\n"
        "                maximum=(move.totalpp rescue 0).to_i\n"
        "                pp=maximum if maximum>0\n"
        "              rescue Exception\n"
        "              end\n"
        "            end\n"
        "          end\n"
        "        move.pp=pp\n"
        "        if move.thismove && move.id==move.thismove.id && !@effects[PBEffects::Transform]\n"
        "          move.thismove.pp=pp\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "    installed=1\n"
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
