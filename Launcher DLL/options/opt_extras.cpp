#include "opt_extras.h"
#include "../rgss_safe_dispatch.h"

namespace {

enum PendingAction {
    ACTION_NONE = 0,
    ACTION_UNLOCK_FLY = 1,
    ACTION_OPEN_PC = 2,
    ACTION_COMPLETE_DEX = 3
};

static volatile LONG s_pending_action = ACTION_NONE;

static void post_to_game() { rgss_safe_dispatch_notify(); }

static const char kUnlockFlyRuby[] =
    "begin\n"
    "  global=(defined?($PokemonGlobal) ? $PokemonGlobal : nil)\n"
    "  maps=(global ? (global.visitedMaps rescue nil) : nil)\n"
    "  raise 'Destinations de vol indisponibles.' if !maps\n"
    "  infos=(load_data('Data/MapInfos.rxdata') rescue nil)\n"
    "  highest=(infos && infos.respond_to?(:keys) ? infos.keys.max.to_i : 999)\n"
    "  highest=999 if highest<=0\n"
    "  1.upto(highest) { |map_id| maps[map_id]=true }\n"
    "rescue Exception\n"
    "end\n";

static const char kOpenPcRuby[] =
    "begin\n"
    "  valid=defined?(Scene_Map) && $scene && $scene.is_a?(Scene_Map)\n"
    "  transferring=(defined?($game_temp) && $game_temp && ($game_temp.player_transferring rescue false))\n"
    "  running=(defined?($game_system) && $game_system && ($game_system.map_interpreter.running? rescue false))\n"
    "  StorageSystemPC.new.access if valid && !transferring && !running && defined?(StorageSystemPC)\n"
    "rescue Exception\n"
    "end\n";

// Cette version d'Uranium expose setSeen/setOwned par ID d'espece. Les tableaux
// de formes et shiny sont completes explicitement, dans le meme format que
// BW_PokedexNestForm, plutot que de se fier a une instance Pokemon temporaire.
static const char kCompleteDexRuby[] =
    "begin\n"
    "  trainer=(defined?($Trainer) ? $Trainer : nil)\n"
    "  raise 'Dresseur indisponible.' if !trainer\n"
    "  maximum=(PBSpecies.maxValue rescue 800).to_i\n"
    "  1.upto(maximum) do |species|\n"
    "    begin\n"
    "      name=(PBSpecies.getName(species) rescue '').to_s\n"
    "      next if name.length==0 || name =~ /^\\?+$/\n"
    "      trainer.setSeen(species) if trainer.respond_to?(:setSeen)\n"
    "      trainer.setOwned(species) if trainer.respond_to?(:setOwned)\n"
    "      trainer.formseen=[] if !trainer.formseen\n"
    "      trainer.formlastseen=[] if !trainer.formlastseen\n"
    "      trainer.formseen[species]=[[],[]] if !trainer.formseen[species]\n"
    "      trainer.formseen[species][0][0]=true\n"
    "      trainer.formseen[species][1][0]=true\n"
    "      trainer.formlastseen[species]=[0,0]\n"
    "      if trainer.respond_to?(:seenShiny) && trainer.respond_to?(:seenShiny=)\n"
    "        shiny=trainer.seenShiny\n"
    "        shiny=[] if !shiny\n"
    "        shiny[species]=[[],[]] if !shiny[species]\n"
    "        shiny[species][0][0]=true\n"
    "        shiny[species][1][0]=true\n"
    "        trainer.seenShiny=shiny\n"
    "      end\n"
    "    rescue Exception\n"
    "    end\n"
    "  end\n"
    "rescue Exception\n"
    "end\n";

static void __cdecl on_game_thread_tick(void*) {
    const LONG action = InterlockedExchange(&s_pending_action, ACTION_NONE);
    if (action == ACTION_NONE) return;
    const char* ruby = action == ACTION_UNLOCK_FLY ? kUnlockFlyRuby :
                       action == ACTION_OPEN_PC ? kOpenPcRuby : kCompleteDexRuby;
    if (rgss_safe_eval(ruby) != 0)
        InterlockedCompareExchange(&s_pending_action, action, ACTION_NONE);
}

static void trigger(PendingAction action) {
    InterlockedExchange(&s_pending_action, action);
    post_to_game();
}

} // namespace

bool opt_extras_init(const char* ini_path) {
    (void)ini_path;
    return rgss_safe_dispatch_register(on_game_thread_tick, NULL);
}

void opt_extras_shutdown() {
    InterlockedExchange(&s_pending_action, ACTION_NONE);
    rgss_safe_dispatch_unregister(on_game_thread_tick, NULL);
}

void opt_extras_unlock_fly_trigger() { trigger(ACTION_UNLOCK_FLY); }
void opt_extras_open_pc_trigger() { trigger(ACTION_OPEN_PC); }
void opt_extras_complete_dex_trigger() { trigger(ACTION_COMPLETE_DEX); }
