#include "opt_extras.h"
#include "../rgss_safe_dispatch.h"

namespace {

enum PendingAction {
    ACTION_NONE = 0,
    ACTION_UNLOCK_FLY = 1,
    ACTION_OPEN_PC = 2,
    ACTION_COMPLETE_DEX = 3,
    ACTION_FLY_ANYWHERE = 4
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
    // Le PC etait auparavant execute depuis le callback graphique du trainer.
    // Il pouvait alors quitter son dialogue sans liberer le joueur. On le place
    // dans l'interpreteur principal, comme un evenement de carte normal :
    // l'interpreteur reste occupe pendant le PC puis se termine proprement.
    "  interpreter=(pbMapInterpreter rescue nil)\n"
    "  interpreter=($game_system.map_interpreter rescue nil) if !interpreter\n"
    "  if defined?(Scene_Map) && $scene && $scene.is_a?(Scene_Map) && interpreter && !interpreter.running? && defined?(RPG::EventCommand)\n"
    "    commands=[RPG::EventCommand.new(355,0,['pbPokeCenterPC(true)']),RPG::EventCommand.new]\n"
    "    interpreter.setup(commands,0)\n"
    "  end\n"
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

// Equivalent exact du gestionnaire Uranium de la Quadcopter, sans son appel
// initial a checkCanUseFly : le choix de destination fonctionne ainsi depuis
// une grotte ou un interieur, apres le chargement complet de la carte.
static const char kFlyAnywhereRuby[] =
    "begin\n"
    "  raise 'Carte indisponible.' if !defined?(PokemonRegionMapScene) || !defined?(PokemonRegionMap)\n"
    "  scene=PokemonRegionMapScene.new(-1,false)\n"
    "  screen=PokemonRegionMap.new(scene)\n"
    "  ret=screen.pbStartFlyScreen\n"
    "  if ret\n"
    "    $PokemonTemp.flydata=ret\n"
    "    pbFlyAnimation(false,0,0,true)\n"
    "    pbFadeOutIn(99999){\n"
    "      $game_screen.start_tone_change(Tone.new(0,0,0,0),0)\n"
    "      Kernel.pbCancelVehicles\n"
    "      $game_temp.player_new_map_id=$PokemonTemp.flydata[0]\n"
    "      $game_temp.player_new_x=$PokemonTemp.flydata[1]\n"
    "      $game_temp.player_new_y=$PokemonTemp.flydata[2]\n"
    "      $PokemonTemp.flydata=nil\n"
    "      $game_temp.player_new_direction=2\n"
    "      $scene.transfer_player\n"
    "      $game_map.autoplay\n"
    "      $game_map.refresh\n"
    "    }\n"
    "    pbFlyAnimation(true,0,0,true)\n"
    "    pbEraseEscapePoint\n"
    "  end\n"
    "rescue Exception\n"
    "end\n";

static void __cdecl on_game_thread_tick(void*) {
    const LONG action = InterlockedExchange(&s_pending_action, ACTION_NONE);
    if (action == ACTION_NONE) return;
    const char* ruby = action == ACTION_UNLOCK_FLY ? kUnlockFlyRuby :
                       action == ACTION_OPEN_PC ? kOpenPcRuby :
                       action == ACTION_COMPLETE_DEX ? kCompleteDexRuby :
                       kFlyAnywhereRuby;
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
void opt_extras_fly_anywhere_trigger() { trigger(ACTION_FLY_ANYWHERE); }
