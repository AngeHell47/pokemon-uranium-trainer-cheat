#include "../options/opt_pokemon_manager.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

enum PokemonCommandType {
    COMMAND_SET_VALUE = 1,
    COMMAND_SET_TEXT,
    COMMAND_CREATE,
    COMMAND_DELETE
};

struct PokemonCommand {
    LONG type;
    PokemonTarget target;
    LONG field;
    LONG sub_index;
    LONG value;
    char text[64];
    PokemonCommand* next;
};

struct PokemonListShared {
    int count;
    int truncated;
    int species_count;
    int nature_count;
    int ability_count;
    int item_count;
    int reserved;
    PokemonListEntry entries[POKEMON_MANAGER_MAX_LIST];
    PokemonSpeciesEntry species[POKEMON_MANAGER_MAX_SPECIES];
    PokemonCatalogEntry natures[POKEMON_MANAGER_MAX_NATURES];
    PokemonCatalogEntry abilities[POKEMON_MANAGER_MAX_ABILITIES];
    PokemonCatalogEntry items[POKEMON_MANAGER_MAX_ITEMS];
};

struct PokemonResultShared {
    int code;
    char message[124];
};

static char s_ini[MAX_PATH] = {};
static INIT_ONCE s_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION s_lock;
static CRITICAL_SECTION s_queue_lock;
static PokemonCommand* s_head = NULL;
static PokemonCommand* s_tail = NULL;

static PokemonListShared s_shared_list = {};
static PokemonDetail s_shared_detail = {};
static PokemonResultShared s_shared_result = {};

static PokemonListEntry s_list[POKEMON_MANAGER_MAX_LIST] = {};
static PokemonSpeciesEntry s_species[POKEMON_MANAGER_MAX_SPECIES] = {};
static PokemonCatalogEntry s_natures[POKEMON_MANAGER_MAX_NATURES] = {};
static PokemonCatalogEntry s_abilities[POKEMON_MANAGER_MAX_ABILITIES] = {};
static PokemonCatalogEntry s_items[POKEMON_MANAGER_MAX_ITEMS] = {};
static PokemonDetail s_detail = {};
static int s_list_count = 0;
static int s_species_count = 0;
static int s_nature_count = 0;
static int s_ability_count = 0;
static int s_item_count = 0;
static bool s_list_truncated = false;
static char s_status[128] = "En attente des donnees du jeu...";
static LONG s_list_revision = 0;
static LONG s_detail_revision = 0;
static LONG s_status_revision = 0;

static PokemonTarget s_selected = {POKEMON_LOCATION_PARTY, 0, 0};
static bool s_has_selected = false;
static volatile LONG s_refresh_list = 0;
static volatile LONG s_refresh_detail = 0;
static volatile LONG s_processing = 0;
static HANDLE s_stop = NULL;
static HANDLE s_timer = NULL;
static int s_timer_phase = 0;

static char s_ruby_list[16384] = {};
static char s_ruby_detail[12288] = {};
static char s_ruby_write[12288] = {};

static BOOL CALLBACK initialize(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&s_lock);
    InitializeCriticalSection(&s_queue_lock);
    return TRUE;
}

static bool ensure_initialized() {
    return InitOnceExecuteOnce(&s_once, initialize, NULL, NULL) != FALSE;
}

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

static void set_status_locked(const char* message) {
    lstrcpynA(s_status, message ? message : "", sizeof(s_status));
    InterlockedIncrement(&s_status_revision);
}

static void set_status(const char* message) {
    if (!ensure_initialized()) return;
    EnterCriticalSection(&s_lock);
    set_status_locked(message);
    LeaveCriticalSection(&s_lock);
}

static void utf8_to_ansi(char* text, int capacity) {
    if (!text || capacity <= 1) return;
    bool has_high_byte = false;
    for (const unsigned char* cursor = (const unsigned char*)text;
         *cursor; ++cursor) {
        if (*cursor >= 0x80) { has_high_byte = true; break; }
    }
    if (!has_high_byte) return;
    wchar_t wide[128] = {};
    const int wide_count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
        wide, (int)(sizeof(wide) / sizeof(wide[0])));
    if (wide_count <= 0) return;
    char converted[256] = {};
    if (WideCharToMultiByte(CP_ACP, 0, wide, -1, converted,
                            sizeof(converted), NULL, NULL) <= 0) return;
    lstrcpynA(text, converted, capacity);
}

static void ruby_escape_copy(char* destination, size_t capacity,
                             const char* source) {
    if (!destination || capacity == 0) return;
    size_t out = 0;
    for (size_t i = 0; source && source[i] && out + 2 < capacity; ++i) {
        const unsigned char ch = (unsigned char)source[i];
        if (ch == '\\' || ch == '"' || ch == '#') {
            destination[out++] = '\\';
            destination[out++] = (char)ch;
        } else if (ch >= 32) {
            destination[out++] = (char)ch;
        }
    }
    destination[out] = '\0';
}

static bool enqueue(const PokemonCommand& value) {
    if (!ensure_initialized()) return false;
    PokemonCommand* command = (PokemonCommand*)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PokemonCommand));
    if (!command) return false;
    *command = value;
    command->next = NULL;

    EnterCriticalSection(&s_queue_lock);
    if (s_tail) s_tail->next = command;
    else s_head = command;
    s_tail = command;
    LeaveCriticalSection(&s_queue_lock);
    post_to_game();
    return true;
}

static bool dequeue(PokemonCommand* out) {
    if (!out || !ensure_initialized()) return false;
    EnterCriticalSection(&s_queue_lock);
    PokemonCommand* command = s_head;
    if (command) {
        s_head = command->next;
        if (!s_head) s_tail = NULL;
    }
    LeaveCriticalSection(&s_queue_lock);
    if (!command) return false;
    *out = *command;
    out->next = NULL;
    HeapFree(GetProcessHeap(), 0, command);
    return true;
}

static void build_ruby_list() {
    _snprintf(s_ruby_list, sizeof(s_ruby_list) - 1,
        "begin\n"
        "  max_records=%d\n"
        "  max_species=%d\n"
        "  max_natures=%d\n"
        "  max_abilities=%d\n"
        "  max_items=%d\n"
        "  entry_size=%u\n"
        "  species_size=%u\n"
        "  catalog_size=%u\n"
        "  records=[]\n"
        "  truncated=0\n"
        "  append_pokemon=lambda do |pkmn,loc,box,slot|\n"
        "    if pkmn && pkmn.class.to_s==\"PokeBattle_Pokemon\"\n"
        "      if records.length>=max_records\n"
        "        truncated=1\n"
        "      else\n"
        "        species=(pkmn.species rescue 0).to_i\n"
        "        name=(pkmn.name rescue '').to_s[0,23].ljust(24,\"\\0\")\n"
        "        species_name=(PBSpecies.getName(species) rescue '').to_s[0,39].ljust(40,\"\\0\")\n"
        "        values=[loc,box,slot,species,(pkmn.level rescue 0).to_i,\n"
        "          (pkmn.hp rescue 0).to_i,(pkmn.totalhp rescue 0).to_i,\n"
        "          (pkmn.gender rescue 0).to_i,((pkmn.isShiny? rescue false) ? 1 : 0),\n"
        "          ((pkmn.isEgg? rescue false) ? 1 : 0)]\n"
        "        records << values.pack(\"l10\")+name+species_name\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  if defined?($Trainer) && $Trainer && $Trainer.party\n"
        "    $Trainer.party.each_with_index { |pkmn,i| append_pokemon.call(pkmn,0,0,i) }\n"
        "  end\n"
        "  storage=(defined?($PokemonStorage) ? $PokemonStorage : nil)\n"
        "  if storage\n"
        "    boxes=(storage.maxBoxes rescue (storage.instance_variable_get(:@boxes)||[]).length rescue 0).to_i\n"
        "    boxes=0 if boxes<0\n"
        "    boxes=100 if boxes>100\n"
        "    boxes.times do |box|\n"
        "      slots=(storage.maxPokemon(box) rescue 30).to_i\n"
        "      slots=30 if slots<=0\n"
        "      slots=120 if slots>120\n"
        "      slots.times do |slot|\n"
        "        pkmn=(storage[box,slot] rescue nil)\n"
        "        append_pokemon.call(pkmn,1,box,slot) if pkmn\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  catalogs=$__uranium_trainer_pokemon_catalogs\n"
        "  if !catalogs\n"
        "    species_records=[]\n"
        "    species_limit=(PBSpecies.maxValue rescue max_species).to_i\n"
        "    species_limit=max_species if species_limit<=0 || species_limit>max_species\n"
        "    1.upto(species_limit) do |id|\n"
        "      name=(PBSpecies.getName(id) rescue '').to_s\n"
        "      if name.length>0 && name !~ /^\\?+$/\n"
        "        species_records << [id].pack(\"l\")+name[0,39].ljust(40,\"\\0\")\n"
        "      end\n"
        "    end\n"
        "    nature_records=[]\n"
        "    nature_limit=(PBNatures.maxValue rescue 24).to_i\n"
        "    0.upto([nature_limit,max_natures-1].min) do |id|\n"
        "      name=(PBNatures.getName(id) rescue '').to_s\n"
        "      nature_records << [id].pack(\"l\")+name[0,63].ljust(64,\"\\0\") if name.length>0\n"
        "    end\n"
        "    ability_records=[]\n"
        "    ability_limit=(PBAbilities.maxValue rescue PBAbilities.getCount-1 rescue max_abilities).to_i\n"
        "    1.upto([ability_limit,max_abilities].min) do |id|\n"
        "      name=(PBAbilities.getName(id) rescue '').to_s\n"
        "      ability_records << [id].pack(\"l\")+name[0,63].ljust(64,\"\\0\") if name.length>0 && name !~ /^\\?+$/\n"
        "    end\n"
        "    item_records=[]\n"
        "    item_limit=(PBItems.maxValue rescue max_items).to_i\n"
        "    1.upto([item_limit,max_items].min) do |id|\n"
        "      name=(PBItems.getName(id) rescue '').to_s\n"
        "      item_records << [id].pack(\"l\")+name[0,63].ljust(64,\"\\0\") if name.length>0 && name !~ /^\\?+$/\n"
        "    end\n"
        "    catalogs=[species_records[0,max_species],nature_records[0,max_natures],ability_records[0,max_abilities],item_records[0,max_items]]\n"
        "    $__uranium_trainer_pokemon_catalogs=catalogs\n"
        "  end\n"
        "  species_records,nature_records,ability_records,item_records=catalogs\n"
        "  header=[records.length,truncated,species_records.length,nature_records.length,ability_records.length,item_records.length,0].pack(\"l7\")\n"
        "  body=records.join\n"
        "  body << \"\\0\"*((max_records-records.length)*entry_size)\n"
        "  body << species_records.join\n"
        "  body << \"\\0\"*((max_species-species_records.length)*species_size)\n"
        "  body << nature_records.join\n"
        "  body << \"\\0\"*((max_natures-nature_records.length)*catalog_size)\n"
        "  body << ability_records.join\n"
        "  body << \"\\0\"*((max_abilities-ability_records.length)*catalog_size)\n"
        "  body << item_records.join\n"
        "  body << \"\\0\"*((max_items-item_records.length)*catalog_size)\n"
        "  data=header+body\n"
        "  Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,data,%u)\n"
        "rescue Exception\n"
        "  begin\n"
        "    data=[0,0,0,0,0,0,0].pack(\"l7\")+\"\\0\"*(%u-28)\n"
        "    Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,data,%u)\n"
        "  rescue Exception\n"
        "  end\n"
        "end\n",
        POKEMON_MANAGER_MAX_LIST,
        POKEMON_MANAGER_MAX_SPECIES,
        POKEMON_MANAGER_MAX_NATURES,
        POKEMON_MANAGER_MAX_ABILITIES,
        POKEMON_MANAGER_MAX_ITEMS,
        (unsigned)sizeof(PokemonListEntry),
        (unsigned)sizeof(PokemonSpeciesEntry),
        (unsigned)sizeof(PokemonCatalogEntry),
        (unsigned long)(ULONG_PTR)&s_shared_list,
        (unsigned)sizeof(s_shared_list),
        (unsigned)sizeof(s_shared_list),
        (unsigned long)(ULONG_PTR)&s_shared_list,
        (unsigned)sizeof(s_shared_list));
    s_ruby_list[sizeof(s_ruby_list) - 1] = '\0';
}

static void build_ruby_detail(const PokemonTarget& target) {
    _snprintf(s_ruby_detail, sizeof(s_ruby_detail) - 1,
        "begin\n"
        "  loc=%d; box=%d; slot=%d\n"
        "  max_forms=%d; catalog_size=%u\n"
        "  pkmn=nil\n"
        "  if loc==0\n"
        "    pkmn=($Trainer.party[slot] rescue nil)\n"
        "  else\n"
        "    pkmn=($PokemonStorage[box,slot] rescue nil)\n"
        "  end\n"
        "  values=[0,loc,box,slot]+[0]*57\n"
        "  strings=\"\\0\"*304\n"
        "  ability_records=[]\n"
        "  ability_index=0\n"
        "  form_records=[]\n"
        "  if pkmn && pkmn.class.to_s==\"PokeBattle_Pokemon\"\n"
        "    species=(pkmn.species rescue 0).to_i\n"
        "    nature=(pkmn.nature rescue pkmn.instance_variable_get(:@nature) rescue 0).to_i\n"
        "    ability=(pkmn.ability rescue 0).to_i\n"
        "    item=(pkmn.item rescue 0).to_i\n"
        "    iv=(pkmn.iv rescue pkmn.instance_variable_get(:@iv) rescue [0]*6)\n"
        "    ev=(pkmn.ev rescue pkmn.instance_variable_get(:@ev) rescue [0]*6)\n"
        "    moves=(pkmn.moves rescue pkmn.instance_variable_get(:@moves) rescue [])\n"
        "    values=[1,loc,box,slot,species,(pkmn.level rescue 0).to_i,\n"
        "      (pkmn.exp rescue 0).to_i,(pkmn.hp rescue 0).to_i,(pkmn.totalhp rescue 0).to_i,\n"
        "      (pkmn.status rescue 0).to_i,(pkmn.statusCount rescue 0).to_i,\n"
        "      (pkmn.gender rescue 0).to_i,(pkmn.form rescue 0).to_i,\n"
        "      ((pkmn.isShiny? rescue false) ? 1 : 0),nature,ability,item,\n"
        "      (pkmn.happiness rescue 0).to_i,(pkmn.pokerus rescue 0).to_i,\n"
        "      ((pkmn.isEgg? rescue false) ? 1 : 0),(pkmn.eggsteps rescue 0).to_i,\n"
        "      (pkmn.ballused rescue pkmn.instance_variable_get(:@ballused) rescue 0).to_i,\n"
        "      (pkmn.markings rescue 0).to_i,\n"
        "      (pkmn.obtainMode rescue pkmn.instance_variable_get(:@obtainMode) rescue 0).to_i,\n"
        "      (pkmn.obtainMap rescue pkmn.instance_variable_get(:@obtainMap) rescue 0).to_i,\n"
        "      (pkmn.obtainLevel rescue pkmn.instance_variable_get(:@obtainLevel) rescue 0).to_i,\n"
        "      (pkmn.attack rescue 0).to_i,(pkmn.defense rescue 0).to_i,\n"
        "      (pkmn.speed rescue 0).to_i,(pkmn.spatk rescue 0).to_i,\n"
        "      (pkmn.spdef rescue 0).to_i,(pkmn.trainerID rescue 0).to_i,\n"
        "      (pkmn.personalID rescue 0).to_i]\n"
        "    6.times { |i| values << iv[i].to_i }\n"
        "    6.times { |i| values << ev[i].to_i }\n"
        "    4.times do |i|\n"
        "      move=moves[i]\n"
        "      values << (move ? (move.id rescue 0).to_i : 0)\n"
        "      values << (move ? (move.pp rescue 0).to_i : 0)\n"
        "      values << (move ? (move.totalpp rescue move.totalPP rescue 0).to_i : 0)\n"
        "      values << (move ? (move.ppup rescue move.instance_variable_get(:@ppup) rescue 0).to_i : 0)\n"
        "    end\n"
        "    name=(pkmn.name rescue '').to_s[0,23].ljust(24,\"\\0\")\n"
        "    species_name=(PBSpecies.getName(species) rescue '').to_s[0,39].ljust(40,\"\\0\")\n"
        "    nature_name=(PBNatures.getName(nature) rescue '').to_s[0,39].ljust(40,\"\\0\")\n"
        "    ability_name=(PBAbilities.getName(ability) rescue '').to_s[0,39].ljust(40,\"\\0\")\n"
        "    item_name=(PBItems.getName(item) rescue '').to_s[0,63].ljust(64,\"\\0\")\n"
        "    ot=(pkmn.ot rescue pkmn.trainerName rescue '').to_s[0,31].ljust(32,\"\\0\")\n"
        "    obtain=(pkmn.obtainText rescue pkmn.instance_variable_get(:@obtainText) rescue '').to_s[0,63].ljust(64,\"\\0\")\n"
        "    strings=name+species_name+nature_name+ability_name+item_name+ot+obtain\n"
        "    ability_index=(pkmn.abilityIndex rescue 0).to_i\n"
        "    probe=(pkmn.clone rescue nil)\n"
        "    seen_abilities={}\n"
        "    if probe\n"
        "      0.upto(5) do |slot_index|\n"
        "        begin; probe.setAbility(slot_index); rescue Exception; next; end\n"
        "        ability_id=(probe.ability rescue 0).to_i\n"
        "        next if ability_id<=0 || seen_abilities[ability_id]\n"
        "        seen_abilities[ability_id]=true\n"
        "        ability_label=(PBAbilities.getName(ability_id) rescue '').to_s\n"
        "        label=(\"#\"+ability_id.to_s+\"  \"+ability_label)[0,63].ljust(64,\"\\0\")\n"
        "        ability_records << [slot_index].pack(\"l\")+label\n"
        "      end\n"
        "    end\n"
        "    raw_forms=(pbGetMessage(MessageTypes::FormNames,species) rescue '').to_s\n"
        "    raw_forms.split(',',-1).each_with_index do |form_name,id|\n"
        "      next if form_name.to_s.length==0\n"
        "      form_records << [id].pack(\"l\")+form_name.to_s[0,63].ljust(64,\"\\0\")\n"
        "    end\n"
        "    current_form=(pkmn.form rescue 0).to_i\n"
        "    if form_records.length==0\n"
        "      form_records << [0].pack(\"l\")+\"Forme normale\".ljust(64,\"\\0\")\n"
        "    end\n"
        "    known=false\n"
        "    form_records.each { |record| known=true if record[0,4].unpack(\"l\")[0]==current_form }\n"
        "    form_records << [current_form].pack(\"l\")+(\"Forme \"+current_form.to_s)[0,63].ljust(64,\"\\0\") if !known\n"
        "    form_records=form_records[0,max_forms]\n"
        "  end\n"
        "  data=values.pack(\"l61\")+strings+[ability_index,ability_records.length].pack(\"l2\")+ability_records.join\n"
        "  data << \"\\0\"*((6-ability_records.length)*catalog_size)\n"
        "  data << [form_records.length].pack(\"l\")+form_records.join\n"
        "  data << \"\\0\"*((max_forms-form_records.length)*catalog_size)\n"
        "  Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,data,%u)\n"
        "rescue Exception\n"
        "  begin\n"
        "    data=([0,%d,%d,%d]+[0]*57).pack(\"l61\")+\"\\0\"*304+[0,0].pack(\"l2\")+\"\\0\"*(6*catalog_size)+[0].pack(\"l\")+\"\\0\"*(max_forms*catalog_size)\n"
        "    Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,data,%u)\n"
        "  rescue Exception\n"
        "  end\n"
        "end\n",
        target.location, target.box, target.slot,
        POKEMON_MANAGER_MAX_FORMS, (unsigned)sizeof(PokemonCatalogEntry),
        (unsigned long)(ULONG_PTR)&s_shared_detail,
        (unsigned)sizeof(s_shared_detail),
        target.location, target.box, target.slot,
        (unsigned long)(ULONG_PTR)&s_shared_detail,
        (unsigned)sizeof(s_shared_detail));
    s_ruby_detail[sizeof(s_ruby_detail) - 1] = '\0';
}

static void append_result_wrapper(char* destination, size_t capacity,
                                  const char* body) {
    _snprintf(destination, capacity - 1,
        "code=0\n"
        "message=\"Modification appliquee.\"\n"
        "begin\n"
        "%s\n"
        "rescue Exception => e\n"
        "  code=-1\n"
        "  detail=(e.message rescue \"Erreur Ruby\").to_s\n"
        "  trace=(e.backtrace rescue [])\n"
        "  where=(trace && trace[0] ? trace[0].to_s : \"\")\n"
        "  message=(where.length>0 ? detail+\" @ \"+where : detail)\n"
        "ensure\n"
        "  begin\n"
        "    text=message.to_s[0,123].ljust(124,\"\\0\")\n"
        "    data=[code].pack(\"l\")+text\n"
        "    Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,data,128)\n"
        "  rescue Exception\n"
        "  end\n"
        "end\n",
        body ? body : "", (unsigned long)(ULONG_PTR)&s_shared_result);
    destination[capacity - 1] = '\0';
}

static void target_prefix(char* destination, size_t capacity,
                          const PokemonTarget& target, const char* action) {
    _snprintf(destination, capacity - 1,
        "  loc=%d; box=%d; slot=%d\n"
        "  pkmn=(loc==0 ? ($Trainer.party[slot] rescue nil) : ($PokemonStorage[box,slot] rescue nil))\n"
        "  if !pkmn || pkmn.class.to_s!=\"PokeBattle_Pokemon\"\n"
        "    code=-2; message=\"Le Pokemon cible n'existe plus.\"\n"
        "  else\n"
        "%s\n"
        "  end",
        target.location, target.box, target.slot, action ? action : "");
    destination[capacity - 1] = '\0';
}

static void build_set_value(const PokemonCommand& command) {
    char action[4096] = {};
    const int value = (int)command.value;
    const int index = (int)command.sub_index;
    switch ((PokemonEditField)command.field) {
    case POKEMON_EDIT_SPECIES:
        _snprintf(action, sizeof(action) - 1,
            "    v=%d\n"
            "    maximum=(PBSpecies.maxValue rescue 9999).to_i\n"
            "    if v<1 || v>maximum\n"
            "      code=-3; message=\"Espece invalide.\"\n"
            "    else\n"
            "      oldhp=(pkmn.hp rescue 0).to_i\n"
            "      pkmn.instance_variable_set(:@species,v)\n"
            "      pkmn.instance_variable_set(:@form,0)\n"
            "      pkmn.calcStats if pkmn.respond_to?(:calcStats)\n"
            "      pkmn.hp=[oldhp,(pkmn.totalhp rescue oldhp).to_i].min if pkmn.respond_to?(:hp=)\n"
            "      message=\"Espece modifiee.\"\n"
            "    end", value);
        break;
    case POKEMON_EDIT_LEVEL:
        _snprintf(action, sizeof(action) - 1,
            "    v=[[1,%d].max,100].min\n"
            "    oldhp=(pkmn.hp rescue 0).to_i\n"
            "    pkmn.level=v if pkmn.respond_to?(:level=)\n"
            "    pkmn.instance_variable_set(:@level,v) unless pkmn.respond_to?(:level=)\n"
            "    pkmn.calcStats if pkmn.respond_to?(:calcStats)\n"
            "    pkmn.hp=[oldhp,(pkmn.totalhp rescue oldhp).to_i].min if pkmn.respond_to?(:hp=)\n"
            "    message=\"Niveau modifie.\"", value);
        break;
    case POKEMON_EDIT_HP:
        _snprintf(action, sizeof(action) - 1,
            "    v=[[0,%d].max,(pkmn.totalhp rescue %d).to_i].min\n"
            "    pkmn.hp=v if pkmn.respond_to?(:hp=)\n"
            "    pkmn.instance_variable_set(:@hp,v) unless pkmn.respond_to?(:hp=)\n"
            "    message=\"PV modifies.\"", value, value);
        break;
    case POKEMON_EDIT_STATUS:
        _snprintf(action, sizeof(action) - 1,
            "    v=[[0,%d].max,99].min\n"
            "    pkmn.status=v if pkmn.respond_to?(:status=)\n"
            "    pkmn.instance_variable_set(:@status,v) unless pkmn.respond_to?(:status=)", value);
        break;
    case POKEMON_EDIT_STATUS_COUNT:
        _snprintf(action, sizeof(action) - 1,
            "    v=[[0,%d].max,999].min\n"
            "    pkmn.statusCount=v if pkmn.respond_to?(:statusCount=)\n"
            "    pkmn.instance_variable_set(:@statusCount,v) unless pkmn.respond_to?(:statusCount=)", value);
        break;
    case POKEMON_EDIT_GENDER:
        _snprintf(action, sizeof(action) - 1,
            "    v=[[0,%d].max,2].min\n"
            "    if pkmn.respond_to?(:setGender); pkmn.setGender(v)\n"
            "    else pkmn.instance_variable_set(:@genderflag,v); end", value);
        break;
    case POKEMON_EDIT_FORM:
        _snprintf(action, sizeof(action) - 1,
            "    v=[[0,%d].max,999].min\n"
            "    if pkmn.respond_to?(:form=); pkmn.form=v\n"
            "    else pkmn.instance_variable_set(:@form,v); end\n"
            "    pkmn.calcStats if pkmn.respond_to?(:calcStats)", value);
        break;
    case POKEMON_EDIT_SHINY:
        _snprintf(action, sizeof(action) - 1,
            "    if %d!=0\n"
            "      pkmn.makeShiny if pkmn.respond_to?(:makeShiny)\n"
            "    else\n"
            "      pkmn.makeNotShiny if pkmn.respond_to?(:makeNotShiny)\n"
            "    end", value);
        break;
    case POKEMON_EDIT_NATURE:
        _snprintf(action, sizeof(action) - 1,
            "    v=[[0,%d].max,999].min\n"
            "    if pkmn.respond_to?(:setNature); pkmn.setNature(v)\n"
            "    elsif pkmn.respond_to?(:nature=); pkmn.nature=v\n"
            "    else pkmn.instance_variable_set(:@nature,v); end\n"
            "    pkmn.calcStats if pkmn.respond_to?(:calcStats)", value);
        break;
    case POKEMON_EDIT_ABILITY:
        _snprintf(action, sizeof(action) - 1,
            "    v=[[0,%d].max,9999].min\n"
            "    if pkmn.respond_to?(:setAbility); pkmn.setAbility(v)\n"
            "    elsif pkmn.respond_to?(:abilityflag=); pkmn.abilityflag=v\n"
            "    else pkmn.instance_variable_set(:@abilityflag,v); end", value);
        break;
    case POKEMON_EDIT_HELD_ITEM:
        _snprintf(action, sizeof(action) - 1,
            "    v=[[0,%d].max,(PBItems.maxValue rescue 9999).to_i].min\n"
            "    if pkmn.respond_to?(:item=); pkmn.item=v\n"
            "    else pkmn.instance_variable_set(:@item,v); end", value);
        break;
    case POKEMON_EDIT_HAPPINESS:
        _snprintf(action, sizeof(action) - 1,
            "    v=[[0,%d].max,255].min\n"
            "    if pkmn.respond_to?(:happiness=); pkmn.happiness=v\n"
            "    else pkmn.instance_variable_set(:@happiness,v); end", value);
        break;
    case POKEMON_EDIT_POKERUS:
        _snprintf(action, sizeof(action) - 1,
            "    v=[[0,%d].max,255].min\n"
            "    if pkmn.respond_to?(:pokerus=); pkmn.pokerus=v\n"
            "    else pkmn.instance_variable_set(:@pokerus,v); end", value);
        break;
    case POKEMON_EDIT_EGGSTEPS:
        _snprintf(action, sizeof(action) - 1,
            "    v=[[0,%d].max,999999].min\n"
            "    if pkmn.respond_to?(:eggsteps=); pkmn.eggsteps=v\n"
            "    else pkmn.instance_variable_set(:@eggsteps,v); end", value);
        break;
    case POKEMON_EDIT_POKEBALL:
        _snprintf(action, sizeof(action) - 1,
            "    v=[[0,%d].max,9999].min\n"
            "    if pkmn.respond_to?(:ballused=); pkmn.ballused=v\n"
            "    else pkmn.instance_variable_set(:@ballused,v); end", value);
        break;
    case POKEMON_EDIT_MARKINGS:
        _snprintf(action, sizeof(action) - 1,
            "    v=[[0,%d].max,255].min\n"
            "    if pkmn.respond_to?(:markings=); pkmn.markings=v\n"
            "    else pkmn.instance_variable_set(:@markings,v); end", value);
        break;
    case POKEMON_EDIT_OBTAIN_MODE:
    case POKEMON_EDIT_OBTAIN_MAP:
    case POKEMON_EDIT_OBTAIN_LEVEL: {
        const char* property = command.field == POKEMON_EDIT_OBTAIN_MODE ? "obtainMode" :
                               command.field == POKEMON_EDIT_OBTAIN_MAP ? "obtainMap" : "obtainLevel";
        _snprintf(action, sizeof(action) - 1,
            "    v=[[0,%d].max,999999].min\n"
            "    if pkmn.respond_to?(:%s=); pkmn.%s=v\n"
            "    else pkmn.instance_variable_set(:@%s,v); end",
            value, property, property, property);
        break;
    }
    case POKEMON_EDIT_IV:
        _snprintf(action, sizeof(action) - 1,
            "    stat=[[0,%d].max,5].min; v=[[0,%d].max,9999].min\n"
            "    arr=(pkmn.iv rescue pkmn.instance_variable_get(:@iv) rescue [0]*6)\n"
            "    arr[stat]=v\n"
            "    if pkmn.respond_to?(:iv=); pkmn.iv=arr\n"
            "    else pkmn.instance_variable_set(:@iv,arr); end\n"
            "    pkmn.calcStats if pkmn.respond_to?(:calcStats)", index, value);
        break;
    case POKEMON_EDIT_EV:
        _snprintf(action, sizeof(action) - 1,
            "    stat=[[0,%d].max,5].min; requested=[[0,%d].max,9999].min\n"
            "    arr=(pkmn.ev rescue pkmn.instance_variable_get(:@ev) rescue [0]*6)\n"
            "    arr[stat]=requested\n"
            "    if pkmn.respond_to?(:ev=); pkmn.ev=arr\n"
            "    else pkmn.instance_variable_set(:@ev,arr); end\n"
            "    pkmn.calcStats if pkmn.respond_to?(:calcStats)", index, value);
        break;
    case POKEMON_EDIT_MOVE_ID:
        _snprintf(action, sizeof(action) - 1,
            "    move_slot=[[0,%d].max,3].min; move_id=[[0,%d].max,9999].min\n"
            "    arr=(pkmn.moves rescue pkmn.instance_variable_get(:@moves) rescue [])\n"
            "    while arr.length<4 do arr << PBMove.new(0) end\n"
            "    arr[move_slot]=PBMove.new(move_id)\n"
            "    if pkmn.respond_to?(:moves=); pkmn.moves=arr\n"
            "    else pkmn.instance_variable_set(:@moves,arr); end", index, value);
        break;
    case POKEMON_EDIT_MOVE_PP:
        _snprintf(action, sizeof(action) - 1,
            "    move_slot=[[0,%d].max,3].min\n"
            "    move=(pkmn.moves[move_slot] rescue nil)\n"
            "    if move\n"
            "      move.pp=[[0,%d].max,9999].min if move.respond_to?(:pp=)\n"
            "    end", index, value);
        break;
    case POKEMON_EDIT_MOVE_PPUP:
        _snprintf(action, sizeof(action) - 1,
            "    move_slot=[[0,%d].max,3].min; v=[[0,%d].max,9999].min\n"
            "    move=(pkmn.moves[move_slot] rescue nil)\n"
            "    if move\n"
            "      if move.respond_to?(:ppup=); move.ppup=v\n"
            "      else move.instance_variable_set(:@ppup,v); end\n"
            "      move.pp=[(move.pp rescue 0).to_i,9999].min if move.respond_to?(:pp=)\n"
            "    end", index, value);
        break;
    default:
        lstrcpyA(action, "    code=-4; message=\"Champ non modifiable.\"");
        break;
    }

    char body[8192] = {};
    target_prefix(body, sizeof(body), command.target, action);
    append_result_wrapper(s_ruby_write, sizeof(s_ruby_write), body);
}

static void build_set_text(const PokemonCommand& command) {
    char escaped[128] = {};
    ruby_escape_copy(escaped, sizeof(escaped), command.text);
    char action[2048] = {};
    if (command.field == POKEMON_EDIT_NAME) {
        _snprintf(action, sizeof(action) - 1,
            "    text=\"%s\".to_s[0,11]\n"
            "    if pkmn.respond_to?(:name=); pkmn.name=text\n"
            "    else pkmn.instance_variable_set(:@name,text); end\n"
            "    message=\"Surnom modifie.\"", escaped);
    } else if (command.field == POKEMON_EDIT_OBTAIN_TEXT) {
        _snprintf(action, sizeof(action) - 1,
            "    text=\"%s\".to_s[0,63]\n"
            "    if pkmn.respond_to?(:obtainText=); pkmn.obtainText=text\n"
            "    else pkmn.instance_variable_set(:@obtainText,text); end\n"
            "    message=\"Texte de provenance modifie.\"", escaped);
    } else {
        lstrcpyA(action, "    code=-4; message=\"Champ texte non modifiable.\"");
    }
    char body[4096] = {};
    target_prefix(body, sizeof(body), command.target, action);
    append_result_wrapper(s_ruby_write, sizeof(s_ruby_write), body);
}

static void build_create(const PokemonCommand& command) {
    char body[6144] = {};
    _snprintf(body, sizeof(body) - 1,
        "  species=%d; level=[[1,%d].max,100].min\n"
        "  maximum=(PBSpecies.maxValue rescue 9999).to_i\n"
        "  if species<1 || species>maximum\n"
        "    code=-3; message=\"Espece invalide.\"\n"
        "  elsif !defined?($Trainer) || !$Trainer\n"
        "    code=-4; message=\"Aucun Dresseur charge.\"\n"
        "  else\n"
        // Uranium conserve l'initialiseur de base sous __mf_initialize avant
        // d'installer son wrapper MultipleForms. Certaines parties restaurees
        // exposent un wrapper initialize sans argument : appeler directement
        // l'alias de base evite cette incompatibilite, puis reproduire la
        // logique getFormOnCreation du wrapper officiel.
        "    begin\n"
        "      base_initializer=::PokeBattle_Pokemon.instance_method(:__mf_initialize)\n"
        "    rescue Exception\n"
        "      base_initializer=nil\n"
        "    end\n"
        "    if base_initializer\n"
        "      pkmn=::PokeBattle_Pokemon.allocate\n"
        "      base_initializer.bind(pkmn).call(species,level,nil,true)\n"
        "      if defined?(::MultipleForms)\n"
        "        created_form=(::MultipleForms.call('getFormOnCreation',pkmn) rescue nil)\n"
        "        if created_form\n"
        "          pkmn.form=created_form\n"
        "          pkmn.resetMoves\n"
        "        end\n"
        "      end\n"
        "    else\n"
        "      pkmn=::PokeBattle_Pokemon.new(species,level,nil)\n"
        "    end\n"
        "    reference=($Trainer.party && $Trainer.party[0] rescue nil)\n"
        "    trainer_id=($Trainer.instance_variable_get(:@id) rescue 0).to_i\n"
        "    trainer_name=($Trainer.instance_variable_get(:@name) rescue '').to_s\n"
        "    trainer_gender=(reference ? (reference.otgender rescue 2) : 2).to_i\n"
        "    trainer_language=(reference ? (reference.language rescue 0) : ($Trainer.instance_variable_get(:@language) rescue 0)).to_i\n"
        "    pkmn.instance_variable_set(:@trainerID,trainer_id)\n"
        "    pkmn.instance_variable_set(:@ot,trainer_name)\n"
        "    pkmn.instance_variable_set(:@otgender,trainer_gender)\n"
        "    pkmn.instance_variable_set(:@language,trainer_language)\n"
        "    pkmn.calcStats if pkmn.respond_to?(:calcStats)\n"
        "    if $Trainer.party && $Trainer.party.length<6\n"
        "      $Trainer.party << pkmn\n"
        "      message=\"Pokemon ajoute a l'equipe.\"\n"
        "    else\n"
        "      storage=(defined?($PokemonStorage) ? $PokemonStorage : nil)\n"
        "      stored=false\n"
        "      if storage && storage.respond_to?(:pbStoreCaught)\n"
        "        result=storage.pbStoreCaught(pkmn)\n"
        "        stored=(result!=nil && result!=false && result!=-1)\n"
        "      elsif storage\n"
        "        boxes=(storage.maxBoxes rescue 0).to_i\n"
        "        boxes.times do |box|\n"
        "          break if stored\n"
        "          slots=(storage.maxPokemon(box) rescue 30).to_i\n"
        "          slots.times do |slot|\n"
        "            if (storage[box,slot] rescue nil)==nil\n"
        "              storage[box,slot]=pkmn\n"
        "              stored=true\n"
        "              break\n"
        "            end\n"
        "          end\n"
        "        end\n"
        "      end\n"
        "      if stored\n"
        "        message=\"Equipe pleine : Pokemon ajoute dans une boite.\"\n"
        "      else\n"
        "        code=-5; message=\"Equipe et stockage pleins ou indisponibles.\"\n"
        "      end\n"
        "    end\n"
        "    if code==0\n"
        "      $Trainer.seen[species]=true if $Trainer.respond_to?(:seen) && $Trainer.seen\n"
        "      $Trainer.owned[species]=true if $Trainer.respond_to?(:owned) && $Trainer.owned\n"
        "      pbSeenForm(pkmn) rescue nil\n"
        "    end\n"
        "  end", (int)command.value, (int)command.sub_index);
    append_result_wrapper(s_ruby_write, sizeof(s_ruby_write), body);
}

static void build_delete(const PokemonCommand& command) {
    char body[4096] = {};
    _snprintf(body, sizeof(body) - 1,
        "  loc=%d; box=%d; slot=%d\n"
        "  if loc==0\n"
        "    party=($Trainer.party rescue nil)\n"
        "    if !party || !party[slot]\n"
        "      code=-2; message=\"Le Pokemon cible n'existe plus.\"\n"
        "    elsif party.length<=1\n"
        "      code=-3; message=\"Impossible de supprimer le dernier Pokemon de l'equipe.\"\n"
        "    else\n"
        "      party.delete_at(slot)\n"
        "      message=\"Pokemon supprime de l'equipe.\"\n"
        "    end\n"
        "  else\n"
        "    storage=(defined?($PokemonStorage) ? $PokemonStorage : nil)\n"
        "    pkmn=(storage[box,slot] rescue nil)\n"
        "    if !storage || !pkmn\n"
        "      code=-2; message=\"Le Pokemon cible n'existe plus.\"\n"
        "    else\n"
        "      storage[box,slot]=nil\n"
        "      message=\"Pokemon supprime de la boite.\"\n"
        "    end\n"
        "  end", command.target.location, command.target.box, command.target.slot);
    append_result_wrapper(s_ruby_write, sizeof(s_ruby_write), body);
}

static void update_result() {
    s_shared_result.message[sizeof(s_shared_result.message) - 1] = '\0';
    char message[128] = {};
    if (s_shared_result.code == 0) {
        lstrcpynA(message, s_shared_result.message, sizeof(message));
    } else {
        _snprintf(message, sizeof(message) - 1, "Erreur (%d) : %s",
                  s_shared_result.code, s_shared_result.message);
        message[sizeof(message) - 1] = '\0';
    }
    set_status(message);
}

static void execute_command(const PokemonCommand& command) {
    memset(&s_shared_result, 0, sizeof(s_shared_result));
    switch ((PokemonCommandType)command.type) {
    case COMMAND_SET_VALUE: build_set_value(command); break;
    case COMMAND_SET_TEXT:  build_set_text(command); break;
    case COMMAND_CREATE:    build_create(command); break;
    case COMMAND_DELETE:    build_delete(command); break;
    default: return;
    }
    rgss_safe_eval(s_ruby_write);
    update_result();
}

static void copy_list_from_shared() {
    int count = s_shared_list.count;
    int species_count = s_shared_list.species_count;
    int nature_count = s_shared_list.nature_count;
    int ability_count = s_shared_list.ability_count;
    int item_count = s_shared_list.item_count;
    if (count < 0) count = 0;
    if (count > POKEMON_MANAGER_MAX_LIST) count = POKEMON_MANAGER_MAX_LIST;
    if (species_count < 0) species_count = 0;
    if (species_count > POKEMON_MANAGER_MAX_SPECIES)
        species_count = POKEMON_MANAGER_MAX_SPECIES;
    if (nature_count < 0) nature_count = 0;
    if (nature_count > POKEMON_MANAGER_MAX_NATURES)
        nature_count = POKEMON_MANAGER_MAX_NATURES;
    if (ability_count < 0) ability_count = 0;
    if (ability_count > POKEMON_MANAGER_MAX_ABILITIES)
        ability_count = POKEMON_MANAGER_MAX_ABILITIES;
    if (item_count < 0) item_count = 0;
    if (item_count > POKEMON_MANAGER_MAX_ITEMS)
        item_count = POKEMON_MANAGER_MAX_ITEMS;

    EnterCriticalSection(&s_lock);
    s_list_count = count;
    s_species_count = species_count;
    s_nature_count = nature_count;
    s_ability_count = ability_count;
    s_item_count = item_count;
    s_list_truncated = s_shared_list.truncated != 0;
    if (count > 0)
        memcpy(s_list, s_shared_list.entries,
               (size_t)count * sizeof(PokemonListEntry));
    if (species_count > 0)
        memcpy(s_species, s_shared_list.species,
               (size_t)species_count * sizeof(PokemonSpeciesEntry));
    if (nature_count > 0)
        memcpy(s_natures, s_shared_list.natures,
               (size_t)nature_count * sizeof(PokemonCatalogEntry));
    if (ability_count > 0)
        memcpy(s_abilities, s_shared_list.abilities,
               (size_t)ability_count * sizeof(PokemonCatalogEntry));
    if (item_count > 0)
        memcpy(s_items, s_shared_list.items,
               (size_t)item_count * sizeof(PokemonCatalogEntry));
    for (int i = 0; i < count; ++i) {
        s_list[i].name[sizeof(s_list[i].name) - 1] = '\0';
        s_list[i].species_name[sizeof(s_list[i].species_name) - 1] = '\0';
        utf8_to_ansi(s_list[i].name, sizeof(s_list[i].name));
        utf8_to_ansi(s_list[i].species_name, sizeof(s_list[i].species_name));
    }
    for (int i = 0; i < species_count; ++i) {
        s_species[i].name[sizeof(s_species[i].name) - 1] = '\0';
        utf8_to_ansi(s_species[i].name, sizeof(s_species[i].name));
    }
    PokemonCatalogEntry* catalogs[] = {s_natures, s_abilities, s_items};
    const int catalog_counts[] = {nature_count, ability_count, item_count};
    for (int catalog = 0; catalog < 3; ++catalog) {
        for (int i = 0; i < catalog_counts[catalog]; ++i) {
            catalogs[catalog][i].name[sizeof(catalogs[catalog][i].name) - 1] = '\0';
            utf8_to_ansi(catalogs[catalog][i].name,
                         sizeof(catalogs[catalog][i].name));
        }
    }
    const bool first_snapshot = s_list_revision == 0;
    InterlockedIncrement(&s_list_revision);
    LeaveCriticalSection(&s_lock);
    if (first_snapshot) set_status("Equipe et boites synchronisees avec le jeu.");
}

static void copy_detail_from_shared() {
    EnterCriticalSection(&s_lock);
    s_detail = s_shared_detail;
    s_detail.name[sizeof(s_detail.name) - 1] = '\0';
    s_detail.species_name[sizeof(s_detail.species_name) - 1] = '\0';
    s_detail.nature_name[sizeof(s_detail.nature_name) - 1] = '\0';
    s_detail.ability_name[sizeof(s_detail.ability_name) - 1] = '\0';
    s_detail.item_name[sizeof(s_detail.item_name) - 1] = '\0';
    s_detail.original_trainer[sizeof(s_detail.original_trainer) - 1] = '\0';
    s_detail.obtain_text[sizeof(s_detail.obtain_text) - 1] = '\0';
    utf8_to_ansi(s_detail.name, sizeof(s_detail.name));
    utf8_to_ansi(s_detail.species_name, sizeof(s_detail.species_name));
    utf8_to_ansi(s_detail.nature_name, sizeof(s_detail.nature_name));
    utf8_to_ansi(s_detail.ability_name, sizeof(s_detail.ability_name));
    utf8_to_ansi(s_detail.item_name, sizeof(s_detail.item_name));
    utf8_to_ansi(s_detail.original_trainer, sizeof(s_detail.original_trainer));
    utf8_to_ansi(s_detail.obtain_text, sizeof(s_detail.obtain_text));
    if (s_detail.ability_choice_count < 0) s_detail.ability_choice_count = 0;
    if (s_detail.ability_choice_count > 6) s_detail.ability_choice_count = 6;
    for (int i = 0; i < s_detail.ability_choice_count; ++i) {
        s_detail.ability_choices[i].name[
            sizeof(s_detail.ability_choices[i].name) - 1] = '\0';
        utf8_to_ansi(s_detail.ability_choices[i].name,
                     sizeof(s_detail.ability_choices[i].name));
    }
    if (s_detail.form_count < 0) s_detail.form_count = 0;
    if (s_detail.form_count > POKEMON_MANAGER_MAX_FORMS)
        s_detail.form_count = POKEMON_MANAGER_MAX_FORMS;
    for (int i = 0; i < s_detail.form_count; ++i) {
        s_detail.forms[i].name[sizeof(s_detail.forms[i].name) - 1] = '\0';
        utf8_to_ansi(s_detail.forms[i].name,
                     sizeof(s_detail.forms[i].name));
    }
    InterlockedIncrement(&s_detail_revision);
    LeaveCriticalSection(&s_lock);
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedCompareExchange(&s_processing, 1, 0) != 0) return;

    bool edited = false;
    PokemonCommand command = {};
    while (dequeue(&command)) {
        execute_command(command);
        edited = true;
    }

    if (edited) {
        InterlockedExchange(&s_refresh_list, 1);
        InterlockedExchange(&s_refresh_detail, 1);
    }

    if (InterlockedExchange(&s_refresh_list, 0) != 0) {
        memset(&s_shared_list, 0, sizeof(s_shared_list));
        rgss_safe_eval(s_ruby_list);
        copy_list_from_shared();
    }

    if (InterlockedExchange(&s_refresh_detail, 0) != 0) {
        PokemonTarget target = {};
        bool selected = false;
        EnterCriticalSection(&s_lock);
        target = s_selected;
        selected = s_has_selected;
        LeaveCriticalSection(&s_lock);
        if (selected) {
            memset(&s_shared_detail, 0, sizeof(s_shared_detail));
            build_ruby_detail(target);
            rgss_safe_eval(s_ruby_detail);
            copy_detail_from_shared();
        }
    }

    InterlockedExchange(&s_processing, 0);
}

static DWORD WINAPI timer_thread(LPVOID) {
    while (WaitForSingleObject(s_stop, 650) == WAIT_TIMEOUT) {
        ++s_timer_phase;
        InterlockedExchange(&s_refresh_detail, 1);
        if ((s_timer_phase % 4) == 0)
            InterlockedExchange(&s_refresh_list, 1);
        post_to_game();
    }
    return 0;
}

} // namespace

bool opt_pokemon_manager_init(const char* ini_path) {
    if (!ensure_initialized()) return false;
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    build_ruby_list();
    s_stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!s_stop) return false;
    return rgss_safe_dispatch_register(on_game_thread_tick, NULL);
}

void opt_pokemon_manager_start() {
    if (!s_stop || s_timer) return;
    ResetEvent(s_stop);
    InterlockedExchange(&s_refresh_list, 1);
    InterlockedExchange(&s_refresh_detail, 1);
    post_to_game();
    s_timer = CreateThread(NULL, 0, timer_thread, NULL, 0, NULL);
}

void opt_pokemon_manager_stop() {
    if (s_timer) {
        SetEvent(s_stop);
        WaitForSingleObject(s_timer, 2000);
        CloseHandle(s_timer);
        s_timer = NULL;
    }
}

void opt_pokemon_manager_shutdown() {
    opt_pokemon_manager_stop();
    rgss_safe_dispatch_unregister(on_game_thread_tick, NULL);
    if (s_stop) {
        CloseHandle(s_stop);
        s_stop = NULL;
    }
}

void opt_pokemon_manager_refresh() {
    InterlockedExchange(&s_refresh_list, 1);
    InterlockedExchange(&s_refresh_detail, 1);
    post_to_game();
}

void opt_pokemon_manager_select(const PokemonTarget& target) {
    if (!ensure_initialized()) return;
    EnterCriticalSection(&s_lock);
    s_selected = target;
    s_has_selected = true;
    LeaveCriticalSection(&s_lock);
    InterlockedExchange(&s_refresh_detail, 1);
    post_to_game();
}

int opt_pokemon_manager_copy_list(PokemonListEntry* out, int capacity,
                                  LONG* revision, bool* truncated) {
    if (!ensure_initialized() || capacity < 0) return 0;
    EnterCriticalSection(&s_lock);
    int count = s_list_count;
    if (count > capacity) count = capacity;
    if (out && count > 0)
        memcpy(out, s_list, (size_t)count * sizeof(PokemonListEntry));
    if (revision) *revision = s_list_revision;
    if (truncated) *truncated = s_list_truncated;
    LeaveCriticalSection(&s_lock);
    return count;
}

int opt_pokemon_manager_copy_species(PokemonSpeciesEntry* out, int capacity,
                                     LONG* revision) {
    if (!ensure_initialized() || capacity < 0) return 0;
    EnterCriticalSection(&s_lock);
    int count = s_species_count;
    if (count > capacity) count = capacity;
    if (out && count > 0)
        memcpy(out, s_species, (size_t)count * sizeof(PokemonSpeciesEntry));
    if (revision) *revision = s_list_revision;
    LeaveCriticalSection(&s_lock);
    return count;
}

static int copy_catalog(PokemonCatalogEntry* out, int capacity,
                        LONG* revision, const PokemonCatalogEntry* source,
                        const int* source_count) {
    if (!ensure_initialized() || capacity < 0) return 0;
    EnterCriticalSection(&s_lock);
    int count = source_count ? *source_count : 0;
    if (count > capacity) count = capacity;
    if (out && count > 0)
        memcpy(out, source, (size_t)count * sizeof(PokemonCatalogEntry));
    if (revision) *revision = s_list_revision;
    LeaveCriticalSection(&s_lock);
    return count;
}

int opt_pokemon_manager_copy_natures(PokemonCatalogEntry* out, int capacity,
                                      LONG* revision) {
    return copy_catalog(out, capacity, revision, s_natures, &s_nature_count);
}

int opt_pokemon_manager_copy_abilities(PokemonCatalogEntry* out, int capacity,
                                        LONG* revision) {
    return copy_catalog(out, capacity, revision, s_abilities, &s_ability_count);
}

int opt_pokemon_manager_copy_items(PokemonCatalogEntry* out, int capacity,
                                    LONG* revision) {
    return copy_catalog(out, capacity, revision, s_items, &s_item_count);
}

bool opt_pokemon_manager_copy_detail(PokemonDetail* out, LONG* revision) {
    if (!out || !ensure_initialized()) return false;
    EnterCriticalSection(&s_lock);
    *out = s_detail;
    if (revision) *revision = s_detail_revision;
    const bool valid = s_detail.valid != 0;
    LeaveCriticalSection(&s_lock);
    return valid;
}

void opt_pokemon_manager_copy_status(char* out, int capacity, LONG* revision) {
    if (!out || capacity <= 0 || !ensure_initialized()) return;
    EnterCriticalSection(&s_lock);
    lstrcpynA(out, s_status, capacity);
    if (revision) *revision = s_status_revision;
    LeaveCriticalSection(&s_lock);
}

void opt_pokemon_manager_set_value(const PokemonTarget& target,
                                   PokemonEditField field, int sub_index,
                                   int value) {
    PokemonCommand command = {};
    command.type = COMMAND_SET_VALUE;
    command.target = target;
    command.field = field;
    command.sub_index = sub_index;
    command.value = value;
    if (!enqueue(command)) set_status("Impossible de mettre la commande en file.");
}

void opt_pokemon_manager_set_text(const PokemonTarget& target,
                                  PokemonEditField field, const char* text) {
    PokemonCommand command = {};
    command.type = COMMAND_SET_TEXT;
    command.target = target;
    command.field = field;
    lstrcpynA(command.text, text ? text : "", sizeof(command.text));
    if (!enqueue(command)) set_status("Impossible de mettre la commande en file.");
}

void opt_pokemon_manager_create(int species, int level) {
    PokemonCommand command = {};
    command.type = COMMAND_CREATE;
    command.value = species;
    command.sub_index = level;
    if (!enqueue(command)) set_status("Impossible de mettre la commande en file.");
}

void opt_pokemon_manager_delete(const PokemonTarget& target) {
    PokemonCommand command = {};
    command.type = COMMAND_DELETE;
    command.target = target;
    if (!enqueue(command)) set_status("Impossible de mettre la commande en file.");
}
