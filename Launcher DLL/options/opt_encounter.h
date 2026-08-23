#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern bool g_force_next_wild;
extern int  g_forced_wild_species;
extern bool g_wild_level_enabled;
extern int  g_wild_level;
extern bool g_wild_shiny_enabled;
extern int  g_wild_shiny_rate;

enum {
    OPT_ENCOUNTER_CATALOG_MAX_ROWS = 256,
    OPT_ENCOUNTER_SEARCH_MAX_ROWS = 1024
};

struct OptEncounterCatalogRow {
    int type;
    int species;
    int min_level;
    int max_level;
    int rate;
    bool owned;
    bool shiny_seen;
    char species_name[64];
};

struct OptEncounterCatalog {
    LONG revision;
    bool ready;
    int map_id;
    char map_name[128];
    int row_count;
    OptEncounterCatalogRow rows[OPT_ENCOUNTER_CATALOG_MAX_ROWS];
    int search_row_count;
    struct SearchRow {
        int map_id;
        char map_name[128];
        int region;
        int map_x;
        int map_y;
        int map_width;
        int map_height;
        char map_mask[128];
        char map_graphic[64];
        OptEncounterCatalogRow encounter;
    } search_rows[OPT_ENCOUNTER_SEARCH_MAX_ROWS];
};

void opt_encounter_init(const char* ini_path);
void opt_encounter_set_hwnd_and_start(HWND hwnd);
void opt_encounter_toggle_force(bool enabled);
void opt_encounter_set_species(int species);
const char* opt_encounter_species_name();
void opt_encounter_toggle_level(bool enabled);
void opt_encounter_set_level(int level);
void opt_encounter_toggle_shiny(bool enabled);
void opt_encounter_set_shiny_rate(int denominator);
// Synchronise les valeurs modifiees par Ruby et la liste des rencontres de la
// carte courante. Renvoie true lorsque l'overlay doit etre redessine.
bool opt_encounter_refresh_ui();
const OptEncounterCatalog* opt_encounter_catalog();
