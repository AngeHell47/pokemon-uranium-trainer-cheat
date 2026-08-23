#include "opt_encounter.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static char s_ruby[24576] = {};
static char s_species_name[64] = {};

// Ruby publie un instantane TSV dans ce tampon uniquement lors d'un changement
// de carte. Le compteur impair/pair evite que l'overlay lise une publication
// partielle sans faire traverser d'objet Ruby entre les threads.
static char s_catalog_payload[131072] = {};
static volatile LONG s_catalog_sequence = 0;
static volatile LONG s_catalog_size = 0;
static OptEncounterCatalog s_catalog = {};

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
        "encounter_installed=false\n"
        "event_installed=false\n"
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
        "  $__uranium_trainer_catalog_buffer_address=%lu\n"
        "  $__uranium_trainer_catalog_capacity=%lu\n"
        "  $__uranium_trainer_catalog_size_address=%lu\n"
        "  $__uranium_trainer_catalog_sequence_address=%lu\n"
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
        "    encounter_installed=true\n"
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
        "    event_installed=true\n"
        "  end\n"
        // La liste est generee depuis encounters.dat, les poids officiels du
        // moteur et POKERADAREXCLUSIVES. Elle reste donc alignee sur les
        // donnees reellement chargees par cette version du jeu.
        "  unless defined?(::UraniumTrainerEncounterCatalog)\n"
        "    module ::UraniumTrainerEncounterCatalog\n"
        "      class << self\n"
        "        def configure(buffer,capacity,size_address,sequence_address,writer)\n"
        "          @buffer=buffer.to_i\n"
        "          @capacity=capacity.to_i\n"
        "          @size_address=size_address.to_i\n"
        "          @sequence_address=sequence_address.to_i\n"
        "          @writer=writer\n"
        "          @sequence=0 if !@sequence\n"
        "          @all_rows=nil if !defined?(@all_rows)\n"
        "        end\n"
        "        def clean(value)\n"
        "          value.to_s.gsub(/[\\r\\n\\t]+/,\" \")\n"
        "        end\n"
        "        def publish(text)\n"
        "          return if !@writer || @buffer.to_i==0 || @capacity.to_i<2\n"
        "          text=text.to_s[0,@capacity-1]\n"
        "          @sequence=(@sequence.to_i+2)&0x7ffffffe\n"
        "          odd=@sequence|1\n"
        "          @writer.call(@sequence_address,[odd].pack(\"l\"),4)\n"
        "          @writer.call(@buffer,text+\"\\0\",text.length+1)\n"
        "          @writer.call(@size_address,[text.length].pack(\"l\"),4)\n"
        "          @writer.call(@sequence_address,[@sequence].pack(\"l\"),4)\n"
        "        end\n"
        "        def build_index\n"
        "          data=load_data(\"Data/encounters.dat\")\n"
        "          data={} if !data.is_a?(Hash)\n"
        "          infos=load_data(\"Data/MapInfos.rxdata\") rescue nil\n"
        "          townmap=load_data(\"Data/townmap.dat\") rescue []\n"
        "          map_ids=data.keys\n"
        "          if defined?(POKERADAREXCLUSIVES)\n"
        "            POKERADAREXCLUSIVES.each do |radar|\n"
        "              map_ids.push(radar[0].to_i) if radar\n"
        "            end\n"
        "          end\n"
        "          @all_rows=[]\n"
        "          map_ids.uniq.sort.each do |candidate|\n"
        "            groups={}\n"
        "            order=[]\n"
        "            map_data=data[candidate]\n"
        "            if map_data && map_data[1]\n"
        "              enctypes=map_data[1]\n"
        "              all_chances=EncounterTypes::EnctypeChances\n"
        "              for type in 0...all_chances.length\n"
        "                list=enctypes[type]\n"
        "                chances=all_chances[type]\n"
        "                next if !list || !chances\n"
        "                for slot in 0...chances.length\n"
        "                  entry=list[slot]\n"
        "                  next if !entry\n"
        "                  key=[type,entry[0].to_i,entry[1].to_i,entry[2].to_i]\n"
        "                  order.push(key) if !groups.has_key?(key)\n"
        "                  groups[key]=groups[key].to_i+chances[slot].to_i\n"
        "                end\n"
        "              end\n"
        "            end\n"
        "            if defined?(POKERADAREXCLUSIVES)\n"
        "              POKERADAREXCLUSIVES.each do |radar|\n"
        "                next if !radar || radar[0].to_i!=candidate\n"
        "                species=(getID(PBSpecies,radar[2]) rescue radar[2].to_i).to_i\n"
        "                next if species<=0\n"
        "                minimum=radar[3].to_i\n"
        "                maximum=(radar[4] ? radar[4] : radar[3]).to_i\n"
        "                key=[13,species,minimum,maximum]\n"
        "                order.push(key) if !groups.has_key?(key)\n"
        "                groups[key]=groups[key].to_i+radar[1].to_i\n"
        "              end\n"
        "            end\n"
        "            location=(infos && infos[candidate] ? infos[candidate].name : candidate.to_s) rescue candidate.to_s\n"
        "            position=(pbGetMetadata(candidate,MetadataMapPosition) rescue nil)\n"
        "            region=(position && position[0] ? position[0].to_i : -1)\n"
        "            map_x=(position && position[1] ? position[1].to_i : -1)\n"
        "            map_y=(position && position[2] ? position[2].to_i : -1)\n"
        "            size=(pbGetMetadata(candidate,MetadataMapSize) rescue nil)\n"
        "            map_width=(size && size[0].to_i>0 ? size[0].to_i : 1)\n"
        "            mask=(size && size[1] ? size[1].to_s : \"1\")\n"
        "            map_height=[(mask.length.to_f/map_width).ceil,1].max\n"
        "            graphic=(region>=0 && townmap[region] ? townmap[region][1].to_s : \"---\") rescue \"---\"\n"
        "            order.each do |key|\n"
        "              name=(PBSpecies.getName(key[1]) rescue key[1].to_s)\n"
        "              @all_rows.push([candidate,clean(location),key[0],key[1],key[2],key[3],groups[key],clean(name),region,map_x,map_y,map_width,map_height,clean(mask),clean(graphic)])\n"
        "            end\n"
        "          end\n"
        "        end\n"
        "        def status_flags\n"
        "          owned={}\n"
        "          shiny={}\n"
        "          signature=0\n"
        "          maximum=(PBSpecies.maxValue rescue 201).to_i\n"
        "          for species in 1..maximum\n"
        "            caught=($Trainer && $Trainer.owned && $Trainer.owned[species]) ? true : false rescue false\n"
        "            shiny_forms=($Trainer && $Trainer.respond_to?(:seenShiny) && $Trainer.seenShiny ? $Trainer.seenShiny[species] : nil) rescue nil\n"
        "            has_shiny=(shiny_forms && shiny_forms.flatten.include?(true)) ? true : false rescue false\n"
        "            owned[species]=caught\n"
        "            shiny[species]=has_shiny\n"
        "            signature=((signature*33)+(caught ? 1 : 0)+(has_shiny ? 2 : 0))&0x7fffffff\n"
        "          end\n"
        "          [owned,shiny,signature]\n"
        "        end\n"
        "        def poll_status\n"
        "          @status_poll=@status_poll.to_i+1\n"
        "          return if (@status_poll%%60)!=0\n"
        "          flags=status_flags\n"
        "          refresh if flags[2]!=@status_signature\n"
        "        rescue Exception\n"
        "        end\n"
        "        def refresh\n"
        "          map_id=($game_map ? $game_map.map_id.to_i : 0) rescue 0\n"
        "          map_name=($game_map ? $game_map.name : \"\") rescue \"\"\n"
        "          build_index if !@all_rows\n"
        "          if map_id<=0\n"
        "            publish([0,0,clean(map_name)].join(\"\\t\")+\"\\n\")\n"
        "            return\n"
        "          end\n"
        "          flags=status_flags\n"
        "          owned=flags[0]\n"
        "          shiny=flags[1]\n"
        "          @status_signature=flags[2]\n"
        "          lines=[[1,map_id,clean(map_name)].join(\"\\t\")]\n"
        "          @all_rows.each do |row|\n"
        "            state=[owned[row[3]] ? 1 : 0,shiny[row[3]] ? 1 : 0]\n"
        "            if row[0].to_i==map_id\n"
        "              lines.push(([\"C\"]+row[2,6]+state).join(\"\\t\"))\n"
        "            end\n"
        "            lines.push(([\"A\"]+row+state).join(\"\\t\"))\n"
        "          end\n"
        "          publish(lines.join(\"\\n\")+\"\\n\")\n"
        "        rescue Exception\n"
        "          publish([-1,map_id.to_i,clean(map_name)].join(\"\\t\")+\"\\n\") rescue nil\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  UraniumTrainerEncounterCatalog.configure(\n"
        "    $__uranium_trainer_catalog_buffer_address,\n"
        "    $__uranium_trainer_catalog_capacity,\n"
        "    $__uranium_trainer_catalog_size_address,\n"
        "    $__uranium_trainer_catalog_sequence_address,\n"
        "    $__uranium_trainer_encounter_writer)\n"
        "  if defined?(Events) && Events.respond_to?(:onMapChange)\n"
        "    map_event=Events.onMapChange\n"
        "    if $__uranium_trainer_catalog_map_event_id!=map_event.object_id\n"
        "      $__uranium_trainer_catalog_map_event_id=map_event.object_id\n"
        "      map_event += proc do |sender,args|\n"
        "        UraniumTrainerEncounterCatalog.refresh\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  if defined?(Events) && Events.respond_to?(:onEndBattle)\n"
        "    battle_event=Events.onEndBattle\n"
        "    if $__uranium_trainer_catalog_battle_event_id!=battle_event.object_id\n"
        "      $__uranium_trainer_catalog_battle_event_id=battle_event.object_id\n"
        "      battle_event += proc do |sender,args|\n"
        "        UraniumTrainerEncounterCatalog.refresh\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  if defined?(Events) && Events.respond_to?(:onMapUpdate)\n"
        "    update_event=Events.onMapUpdate\n"
        "    if $__uranium_trainer_catalog_update_event_id!=update_event.object_id\n"
        "      $__uranium_trainer_catalog_update_event_id=update_event.object_id\n"
        "      update_event += proc do |sender,args|\n"
        "        UraniumTrainerEncounterCatalog.poll_status\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  UraniumTrainerEncounterCatalog.refresh\n"
        "  installed=1 if encounter_installed && event_installed\n"
        "rescue Exception\n"
        "end\n"
        "begin\n"
        "  $__uranium_trainer_encounter_writer.call(%lu,[installed].pack(\"l\"),4)\n"
        "rescue Exception\n"
        "end\n",
        force, species, force_level, level, force_shiny, shiny_rate,
        (unsigned long)(ULONG_PTR)&s_force_next,
        (unsigned long)(ULONG_PTR)s_species_name,
        (unsigned long)(ULONG_PTR)s_catalog_payload,
        (unsigned long)sizeof(s_catalog_payload),
        (unsigned long)(ULONG_PTR)&s_catalog_size,
        (unsigned long)(ULONG_PTR)&s_catalog_sequence,
        (unsigned long)(ULONG_PTR)&s_installed);
}

static bool parse_catalog_if_changed() {
    const LONG first = InterlockedExchangeAdd(&s_catalog_sequence, 0);
    if ((first & 1) != 0 || first == s_catalog.revision) return false;

    LONG size = InterlockedExchangeAdd(&s_catalog_size, 0);
    if (size < 0) size = 0;
    if (size >= (LONG)sizeof(s_catalog_payload))
        size = (LONG)sizeof(s_catalog_payload) - 1;

    static char snapshot[sizeof(s_catalog_payload)] = {};
    ZeroMemory(snapshot, sizeof(snapshot));
    memcpy(snapshot, s_catalog_payload, (size_t)size);
    snapshot[size] = '\0';
    MemoryBarrier();
    const LONG second = InterlockedExchangeAdd(&s_catalog_sequence, 0);
    if (first != second || (second & 1) != 0) return false;

    static OptEncounterCatalog parsed;
    ZeroMemory(&parsed, sizeof(parsed));
    parsed.revision = second;
    char* line_context = NULL;
    char* line = strtok_s(snapshot, "\n", &line_context);
    if (line) {
        char* field_context = NULL;
        char* status = strtok_s(line, "\t\r", &field_context);
        char* map_id = strtok_s(NULL, "\t\r", &field_context);
        char* map_name = strtok_s(NULL, "\t\r", &field_context);
        parsed.ready = status && atoi(status) == 1;
        parsed.map_id = map_id ? atoi(map_id) : 0;
        lstrcpynA(parsed.map_name, map_name ? map_name : "",
                  ARRAYSIZE(parsed.map_name));
    }

    while ((line = strtok_s(NULL, "\n", &line_context)) != NULL) {
        char* field_context = NULL;
        char* fields[18] = {};
        for (int i = 0; i < ARRAYSIZE(fields); ++i)
            fields[i] = strtok_s(i == 0 ? line : NULL, "\t\r", &field_context);
        if (fields[0] && lstrcmpA(fields[0], "C") == 0 &&
            parsed.row_count < OPT_ENCOUNTER_CATALOG_MAX_ROWS &&
            fields[1] && fields[2] && fields[3] && fields[4] &&
            fields[5] && fields[6] && fields[7] && fields[8]) {
            OptEncounterCatalogRow& row = parsed.rows[parsed.row_count++];
            row.type = atoi(fields[1]);
            row.species = atoi(fields[2]);
            row.min_level = atoi(fields[3]);
            row.max_level = atoi(fields[4]);
            row.rate = atoi(fields[5]);
            lstrcpynA(row.species_name, fields[6], ARRAYSIZE(row.species_name));
            row.owned = atoi(fields[7]) != 0;
            row.shiny_seen = atoi(fields[8]) != 0;
        } else if (fields[0] && lstrcmpA(fields[0], "A") == 0 &&
                   parsed.search_row_count < OPT_ENCOUNTER_SEARCH_MAX_ROWS &&
                   fields[1] && fields[2] && fields[3] && fields[4] &&
                   fields[5] && fields[6] && fields[7] && fields[8] &&
                   fields[9] && fields[10] && fields[11] && fields[12] &&
                   fields[13] && fields[14] && fields[15] && fields[16] &&
                   fields[17]) {
            OptEncounterCatalog::SearchRow& result =
                parsed.search_rows[parsed.search_row_count++];
            result.map_id = atoi(fields[1]);
            lstrcpynA(result.map_name, fields[2], ARRAYSIZE(result.map_name));
            result.encounter.type = atoi(fields[3]);
            result.encounter.species = atoi(fields[4]);
            result.encounter.min_level = atoi(fields[5]);
            result.encounter.max_level = atoi(fields[6]);
            result.encounter.rate = atoi(fields[7]);
            lstrcpynA(result.encounter.species_name, fields[8],
                      ARRAYSIZE(result.encounter.species_name));
            result.region = atoi(fields[9]);
            result.map_x = atoi(fields[10]);
            result.map_y = atoi(fields[11]);
            result.map_width = atoi(fields[12]);
            result.map_height = atoi(fields[13]);
            lstrcpynA(result.map_mask, fields[14],
                      ARRAYSIZE(result.map_mask));
            lstrcpynA(result.map_graphic, fields[15],
                      ARRAYSIZE(result.map_graphic));
            result.encounter.owned = atoi(fields[16]) != 0;
            result.encounter.shiny_seen = atoi(fields[17]) != 0;
        }
    }

    s_catalog = parsed;
    return true;
}

static void __cdecl on_game_thread_tick(void*) {
    // Les globales Ruby suffisent pour modifier les options apres
    // l'installation. Ne jamais reevaluer le script en tache de fond : le
    // retry natif ne le repropose que tant que les deux points d'accroche ne
    // sont pas disponibles, ou sur une demande explicite de l'utilisateur.
    if (InterlockedExchange(&s_pending, 0) == 0) return;
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
    g_wild_shiny_rate = clamp(GetPrivateProfileIntA("Settings", "WildShinyRate", kNativeShinyDenominator, s_ini), 1, kNativeShinyDenominator);
    if (!g_wild_shiny_enabled) g_wild_shiny_rate = kNativeShinyDenominator;
    InterlockedExchange(&s_force_next, g_force_next_wild ? 1 : 0);
    InterlockedExchange(&s_species, g_forced_wild_species);
    InterlockedExchange(&s_level_enabled, g_wild_level_enabled ? 1 : 0);
    InterlockedExchange(&s_level, g_wild_level);
    InterlockedExchange(&s_shiny_enabled, g_wild_shiny_enabled ? 1 : 0);
    InterlockedExchange(&s_shiny_rate, g_wild_shiny_rate);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
    InterlockedExchange(&s_catalog_sequence, 0);
    InterlockedExchange(&s_catalog_size, 0);
    ZeroMemory(s_catalog_payload, sizeof(s_catalog_payload));
    ZeroMemory(&s_catalog, sizeof(s_catalog));
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
    g_wild_shiny_rate = clamp(denominator, 1, kNativeShinyDenominator);
    InterlockedExchange(&s_shiny_rate, g_wild_shiny_rate);
    write_int("WildShinyRate", g_wild_shiny_rate);
    if (g_wild_shiny_enabled) queue_apply();
}

bool opt_encounter_refresh_ui() {
    const bool force = InterlockedExchangeAdd(&s_force_next, 0) != 0;
    const bool catalog_changed = parse_catalog_if_changed();
    const bool changed = force != g_force_next_wild || catalog_changed;
    g_force_next_wild = force;
    return changed;
}

const OptEncounterCatalog* opt_encounter_catalog() {
    return &s_catalog;
}
