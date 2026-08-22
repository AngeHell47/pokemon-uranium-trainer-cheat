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

constexpr int kMaxSpecies = 800;
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

static void post_to_game() { rgss_safe_dispatch_notify(); }

static int clamp(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
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
        "  $__uranium_trainer_encounter_writer ||= Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  if defined?(PokemonEncounters)\n"
        "    class PokemonEncounters\n"
        "      unless instance_methods.collect { |m| m.to_s }.include?(\"__uranium_trainer_original_pbEncounteredPokemon\")\n"
        "        alias_method :__uranium_trainer_original_pbEncounteredPokemon, :pbEncounteredPokemon\n"
        "      end\n"
        "      def pbEncounteredPokemon(*args)\n"
        "        encounter=__uranium_trainer_original_pbEncounteredPokemon(*args)\n"
        "        if encounter && $__uranium_trainer_force_next_wild\n"
        "          encounter=encounter.dup\n"
        "          encounter[0]=$__uranium_trainer_force_species\n"
        "          $__uranium_trainer_force_next_wild=false\n"
        "          $__uranium_trainer_encounter_writer.call($__uranium_trainer_encounter_force_address,[0].pack(\"l\"),4)\n"
        "        end\n"
        "        encounter[1]=$__uranium_trainer_wild_level if encounter && $__uranium_trainer_wild_level_enabled\n"
        "        encounter\n"
        "      end\n"
        "    end\n"
        "    installed=1\n"
        "  end\n"
        "  has_wild=(Object.private_method_defined?(:pbGenerateWildPokemon) || Object.method_defined?(:pbGenerateWildPokemon))\n"
        "  if has_wild\n"
        "    has_original=(Object.private_method_defined?(:__uranium_trainer_original_pbGenerateWildPokemon) || Object.method_defined?(:__uranium_trainer_original_pbGenerateWildPokemon))\n"
        "    if !has_original\n"
        "      Object.send(:alias_method,:__uranium_trainer_original_pbGenerateWildPokemon,:pbGenerateWildPokemon)\n"
        "      Object.send(:define_method,:pbGenerateWildPokemon) do |species,level|\n"
        "        pkmn=__uranium_trainer_original_pbGenerateWildPokemon(species,level)\n"
        "        if $__uranium_trainer_wild_shiny_enabled && pkmn\n"
        "          wanted=(rand($__uranium_trainer_wild_shiny_rate)==0)\n"
        "          if wanted\n"
        "            pkmn.makeShiny if pkmn.respond_to?(:makeShiny)\n"
        "          elsif (pkmn.isShiny? rescue false)\n"
        "            tries=0\n"
        "            begin\n"
        "              pkmn.personalID=rand(65536)|(rand(65536)<<16)\n"
        "              tries+=1\n"
        "            end while (pkmn.isShiny? rescue false) && tries<32\n"
        "          end\n"
        "        end\n"
        "        pkmn\n"
        "      end\n"
        "      Object.send(:private,:pbGenerateWildPokemon)\n"
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
    while (InterlockedExchangeAdd(&s_installed, 0) == 0) {
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
    g_forced_wild_species = clamp(GetPrivateProfileIntA("Settings", "ForcedWildSpecies", 1, s_ini), 1, kMaxSpecies);
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
    g_forced_wild_species = clamp(species, 1, kMaxSpecies);
    InterlockedExchange(&s_species, g_forced_wild_species);
    write_int("ForcedWildSpecies", g_forced_wild_species);
    queue_apply();
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
