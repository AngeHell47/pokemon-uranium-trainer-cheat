#include "opt_encounter.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

bool g_force_next_wild = false;
int  g_forced_wild_species = 1;
bool g_wild_level_enabled = false;
int  g_wild_level = 1;
bool g_wild_shiny_enabled = false;
int  g_wild_shiny_rate = 1024;

namespace {

// Pokemon Uranium 1.2.9 expose 201 especes dans PBSpecies. Les identifiants
// du Pokedex national (par exemple 574) ne sont pas des identifiants valides
// de cette edition et feraient echouer Pokemon.new pendant la rencontre.
constexpr int kMaxSpecies = 201;
constexpr int kNativeShinyDenominator = 1024;

static char s_ini[MAX_PATH] = {};
static volatile LONG s_force_next = 0;
static volatile LONG s_species = 1;
static volatile LONG s_level_enabled = 0;
static volatile LONG s_level = 1;
static volatile LONG s_shiny_enabled = 0;
static volatile LONG s_shiny_rate = kNativeShinyDenominator;
static volatile LONG s_pending = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;
static LONG s_refresh_frames = 0;
static char s_ruby[8192] = {};
static char s_species_name[64] = {};

static void post_to_game() { rgss_safe_dispatch_notify(); }

static int clamp(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static int valid_species_or_default(int value) {
    return value >= 1 && value <= kMaxSpecies ? value : 1;
}

static void write_int(const char* key, int value) {
    char text[16] = {};
    wsprintfA(text, "%d", value);
    WritePrivateProfileStringA("Settings", key, text, s_ini);
}

// Les deux interceptions restent volontairement distinctes : choisir une
// espece/niveau ne touche que les tirages aleatoires de PokemonEncounters,
// tandis que le taux shiny s'applique au dernier point commun de creation d'un
// Pokemon sauvage. Les combats scripts qui appellent directement pbWildBattle
// ne sont donc pas detournes pour l'espece ou le niveau.
static void build_ruby() {
    const int species = (int)InterlockedExchangeAdd(&s_species, 0);
    const int level = (int)InterlockedExchangeAdd(&s_level, 0);
    const int shiny_rate = (int)InterlockedExchangeAdd(&s_shiny_rate, 0);
    const char* force = InterlockedExchangeAdd(&s_force_next, 0) ? "true" : "false";
    const char* force_level = InterlockedExchangeAdd(&s_level_enabled, 0) ? "true" : "false";
    const char* force_shiny = InterlockedExchangeAdd(&s_shiny_enabled, 0) ? "true" : "false";

    _snprintf_s(s_ruby, sizeof(s_ruby), _TRUNCATE,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_force_next_wild=%s\n"
        "  $__uranium_trainer_force_species=%d\n"
        "  $__uranium_trainer_wild_level_enabled=%s\n"
        "  $__uranium_trainer_wild_level=%d\n"
        "  $__uranium_trainer_wild_shiny_enabled=%s\n"
        "  $__uranium_trainer_wild_shiny_rate=%d\n"
        "  $__uranium_trainer_encounter_force_address=%lu\n"
        "  $__uranium_trainer_encounter_name_address=%lu\n"
        "  $__uranium_trainer_encounter_writer ||= Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        // Dans cette build, pbEncounteredPokemon est defini sur le singleton
        // de $PokemonEncounters, pas sur PokemonEncounters lui-meme. Le
        // wrapper de classe precedent ne pouvait donc jamais l'intercepter.
        "  encounters=(defined?($PokemonEncounters) ? $PokemonEncounters : nil)\n"
        "  begin\n"
        "    species_name=(PBSpecies.getName($__uranium_trainer_force_species) rescue '').to_s[0,63].ljust(64,\"\\0\")\n"
        "    $__uranium_trainer_encounter_writer.call($__uranium_trainer_encounter_name_address,species_name,64)\n"
        "  rescue Exception\n"
        "  end\n"
        "  if encounters && encounters.respond_to?(:pbEncounteredPokemon)\n"
        "    class << encounters\n"
        "      unless instance_methods.collect { |m| m.to_s }.include?(\"__uranium_trainer_original_pbEncounteredPokemon\")\n"
        "        alias_method :__uranium_trainer_original_pbEncounteredPokemon, :pbEncounteredPokemon\n"
        "      end\n"
        "      def pbEncounteredPokemon(*args)\n"
        "        encounter=__uranium_trainer_original_pbEncounteredPokemon(*args)\n"
        "        if encounter && $__uranium_trainer_force_next_wild\n"
        "          wanted=$__uranium_trainer_force_species.to_i\n"
        "          maximum=(PBSpecies.maxValue rescue 0).to_i\n"
        "          if wanted>=1 && maximum>0 && wanted<=maximum\n"
        "            encounter=encounter.dup\n"
        "            encounter[0]=wanted\n"
        "          end\n"
        "          $__uranium_trainer_force_next_wild=false\n"
        "          $__uranium_trainer_encounter_writer.call($__uranium_trainer_encounter_force_address,[0].pack(\"l\"),4)\n"
        "        end\n"
        "        encounter\n"
        "      end\n"
        "    end\n"
        "    installed=1\n"
        "  end\n"
        // L'evenement officiel est le point final commun de creation. Il
        // donne un niveau strict (apres Pressure) et une probabilite shiny
        // exacte, sans affecter les Pokemon de dresseurs.
        "  if defined?(Events) && Events.respond_to?(:onWildPokemonCreate)\n"
        "    wild_event=Events.onWildPokemonCreate\n"
        "    if $__uranium_trainer_wild_event_id != wild_event.object_id\n"
        "      $__uranium_trainer_wild_event_id=wild_event.object_id\n"
        "      wild_event += proc do |sender,args|\n"
        "        pkmn=(args && args[0] ? args[0] : nil)\n"
        "        if pkmn\n"
        "          if $__uranium_trainer_wild_level_enabled\n"
        "            pkmn.level=$__uranium_trainer_wild_level\n"
        "            pkmn.calcStats if pkmn.respond_to?(:calcStats)\n"
        "          end\n"
        "          if $__uranium_trainer_wild_shiny_enabled\n"
        "            if rand($__uranium_trainer_wild_shiny_rate)==0\n"
        "              pkmn.makeShiny if pkmn.respond_to?(:makeShiny)\n"
        "            else\n"
        "              pkmn.makeNotShiny if pkmn.respond_to?(:makeNotShiny)\n"
        "            end\n"
        "          end\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "    installed=1\n"
        "  end\n"
        "rescue Exception\n"
        "end\n"
        "begin\n"
        "  $__uranium_trainer_encounter_writer.call(%lu,[installed].pack(\"l\"),4)\n"
        "rescue Exception\n"
        "end\n",
        force, species, force_level, level, force_shiny, shiny_rate,
        (unsigned long)(ULONG_PTR)&s_force_next,
        (unsigned long)(ULONG_PTR)s_species_name,
        (unsigned long)(ULONG_PTR)&s_installed);
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_pending, 0) == 0) {
        // Les scripts de rencontres sont recharges apres l'arrivee sur la
        // carte dans certaines sauvegardes. Reposer les deux petits wrappers
        // les rend disponibles quelle que soit la phase de chargement.
        if (++s_refresh_frames < 30) return;
    }
    s_refresh_frames = 0;
    build_ruby();
    if (rgss_safe_eval(s_ruby) != 0) InterlockedExchange(&s_pending, 1);
}

static DWORD WINAPI retry_thread(LPVOID) {
    while (InterlockedExchangeAdd(&s_installed, 0) == 0 &&
           !rgss_safe_dispatch_is_stopping()) {
        InterlockedExchange(&s_pending, 1);
        post_to_game();
        Sleep(500);
    }
    InterlockedExchange(&s_retry_started, 0);
    return 0;
}

static void queue_apply() {
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    if (InterlockedExchangeAdd(&s_installed, 0) == 0 &&
        InterlockedCompareExchange(&s_retry_started, 1, 0) == 0) {
        HANDLE thread = CreateThread(NULL, 0, retry_thread, NULL, 0, NULL);
        if (thread) CloseHandle(thread);
        else InterlockedExchange(&s_retry_started, 0);
    }
}

} // namespace

void opt_encounter_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_force_next_wild = GetPrivateProfileIntA("Settings", "ForceNextWild", 0, s_ini) != 0;
    const int saved_species =
        GetPrivateProfileIntA("Settings", "ForcedWildSpecies", 1, s_ini);
    g_forced_wild_species = valid_species_or_default(saved_species);
    if (saved_species != g_forced_wild_species)
        write_int("ForcedWildSpecies", g_forced_wild_species);
    g_wild_level_enabled = GetPrivateProfileIntA("Settings", "WildLevelEnabled", 0, s_ini) != 0;
    g_wild_level = clamp(GetPrivateProfileIntA("Settings", "WildLevel", 1, s_ini), 1, 100);
    g_wild_shiny_enabled = GetPrivateProfileIntA("Settings", "WildShinyEnabled", 0, s_ini) != 0;
    g_wild_shiny_rate = clamp(GetPrivateProfileIntA("Settings", "WildShinyRate", kNativeShinyDenominator, s_ini), 1, 8192);
    if (!g_wild_shiny_enabled) g_wild_shiny_rate = kNativeShinyDenominator;
    InterlockedExchange(&s_force_next, g_force_next_wild ? 1 : 0);
    InterlockedExchange(&s_species, g_forced_wild_species);
    InterlockedExchange(&s_level_enabled, g_wild_level_enabled ? 1 : 0);
    InterlockedExchange(&s_level, g_wild_level);
    InterlockedExchange(&s_shiny_enabled, g_wild_shiny_enabled ? 1 : 0);
    InterlockedExchange(&s_shiny_rate, g_wild_shiny_rate);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
    s_refresh_frames = 0;
}

void opt_encounter_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    queue_apply();
}

void opt_encounter_toggle_force(bool enabled) {
    g_force_next_wild = enabled;
    InterlockedExchange(&s_force_next, enabled ? 1 : 0);
    WritePrivateProfileStringA("Settings", "ForceNextWild", enabled ? "1" : "0", s_ini);
    queue_apply();
}

void opt_encounter_set_species(int species) {
    g_forced_wild_species = valid_species_or_default(species);
    InterlockedExchange(&s_species, g_forced_wild_species);
    write_int("ForcedWildSpecies", g_forced_wild_species);
    queue_apply();
}

const char* opt_encounter_species_name() {
    return s_species_name[0] ? s_species_name : "Unknown";
}

void opt_encounter_toggle_level(bool enabled) {
    g_wild_level_enabled = enabled;
    InterlockedExchange(&s_level_enabled, enabled ? 1 : 0);
    WritePrivateProfileStringA("Settings", "WildLevelEnabled", enabled ? "1" : "0", s_ini);
    queue_apply();
}

void opt_encounter_set_level(int level) {
    g_wild_level = clamp(level, 1, 100);
    InterlockedExchange(&s_level, g_wild_level);
    write_int("WildLevel", g_wild_level);
    queue_apply();
}

void opt_encounter_toggle_shiny(bool enabled) {
    g_wild_shiny_enabled = enabled;
    if (!enabled) g_wild_shiny_rate = kNativeShinyDenominator;
    InterlockedExchange(&s_shiny_enabled, enabled ? 1 : 0);
    InterlockedExchange(&s_shiny_rate, g_wild_shiny_rate);
    WritePrivateProfileStringA("Settings", "WildShinyEnabled", enabled ? "1" : "0", s_ini);
    write_int("WildShinyRate", g_wild_shiny_rate);
    queue_apply();
}

void opt_encounter_set_shiny_rate(int denominator) {
    g_wild_shiny_rate = clamp(denominator, 1, 8192);
    InterlockedExchange(&s_shiny_rate, g_wild_shiny_rate);
    write_int("WildShinyRate", g_wild_shiny_rate);
    if (g_wild_shiny_enabled) queue_apply();
}

void opt_encounter_refresh_ui() {
    g_force_next_wild = InterlockedExchangeAdd(&s_force_next, 0) != 0;
}
