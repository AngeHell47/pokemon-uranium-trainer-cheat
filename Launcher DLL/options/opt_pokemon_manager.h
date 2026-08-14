#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum PokemonLocation {
    POKEMON_LOCATION_PARTY = 0,
    POKEMON_LOCATION_BOX   = 1
};

struct PokemonTarget {
    int location;
    int box;
    int slot;
};

struct PokemonListEntry {
    int location;
    int box;
    int slot;
    int species;
    int level;
    int hp;
    int totalhp;
    int gender;
    int shiny;
    int egg;
    char name[24];
    char species_name[40];
};

struct PokemonSpeciesEntry {
    int id;
    char name[40];
};

struct PokemonMoveDetail {
    int id;
    int pp;
    int totalpp;
    int ppup;
};

struct PokemonDetail {
    int valid;
    int location;
    int box;
    int slot;
    int species;
    int level;
    int exp;
    int hp;
    int totalhp;
    int status;
    int status_count;
    int gender;
    int form;
    int shiny;
    int nature;
    int ability;
    int held_item;
    int happiness;
    int pokerus;
    int egg;
    int eggsteps;
    int pokeball;
    int markings;
    int obtain_mode;
    int obtain_map;
    int obtain_level;
    int attack;
    int defense;
    int speed;
    int spatk;
    int spdef;
    int trainer_id;
    int personal_id;
    int iv[6];
    int ev[6];
    PokemonMoveDetail moves[4];
    char name[24];
    char species_name[40];
    char nature_name[40];
    char ability_name[40];
    char item_name[64];
    char original_trainer[32];
    char obtain_text[64];
};

enum PokemonEditField {
    POKEMON_EDIT_NAME = 1,
    POKEMON_EDIT_SPECIES,
    POKEMON_EDIT_LEVEL,
    POKEMON_EDIT_HP,
    POKEMON_EDIT_STATUS,
    POKEMON_EDIT_STATUS_COUNT,
    POKEMON_EDIT_GENDER,
    POKEMON_EDIT_FORM,
    POKEMON_EDIT_SHINY,
    POKEMON_EDIT_NATURE,
    POKEMON_EDIT_ABILITY,
    POKEMON_EDIT_HELD_ITEM,
    POKEMON_EDIT_HAPPINESS,
    POKEMON_EDIT_POKERUS,
    POKEMON_EDIT_EGGSTEPS,
    POKEMON_EDIT_POKEBALL,
    POKEMON_EDIT_MARKINGS,
    POKEMON_EDIT_OBTAIN_MODE,
    POKEMON_EDIT_OBTAIN_MAP,
    POKEMON_EDIT_OBTAIN_LEVEL,
    POKEMON_EDIT_OBTAIN_TEXT,
    POKEMON_EDIT_IV,
    POKEMON_EDIT_EV,
    POKEMON_EDIT_MOVE_ID,
    POKEMON_EDIT_MOVE_PP,
    POKEMON_EDIT_MOVE_PPUP
};

enum {
    POKEMON_MANAGER_MAX_LIST = 1024,
    POKEMON_MANAGER_MAX_SPECIES = 800
};

bool opt_pokemon_manager_init(const char* ini_path);
void opt_pokemon_manager_start();
void opt_pokemon_manager_stop();
void opt_pokemon_manager_shutdown();

void opt_pokemon_manager_refresh();
void opt_pokemon_manager_select(const PokemonTarget& target);
int  opt_pokemon_manager_copy_list(PokemonListEntry* out, int capacity,
                                   LONG* revision, bool* truncated);
int  opt_pokemon_manager_copy_species(PokemonSpeciesEntry* out, int capacity,
                                      LONG* revision);
bool opt_pokemon_manager_copy_detail(PokemonDetail* out, LONG* revision);
void opt_pokemon_manager_copy_status(char* out, int capacity, LONG* revision);

void opt_pokemon_manager_set_value(const PokemonTarget& target,
                                   PokemonEditField field, int sub_index,
                                   int value);
void opt_pokemon_manager_set_text(const PokemonTarget& target,
                                  PokemonEditField field, const char* text);
void opt_pokemon_manager_create(int species, int level);
void opt_pokemon_manager_delete(const PokemonTarget& target);
