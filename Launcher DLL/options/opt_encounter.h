#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern bool g_force_next_wild;
extern int  g_forced_wild_species;
extern bool g_wild_level_enabled;
extern int  g_wild_level;
extern bool g_wild_shiny_enabled;
extern int  g_wild_shiny_rate;

void opt_encounter_init(const char* ini_path);
void opt_encounter_set_hwnd_and_start(HWND hwnd);
void opt_encounter_toggle_force(bool enabled);
void opt_encounter_set_species(int species);
const char* opt_encounter_species_name();
void opt_encounter_toggle_level(bool enabled);
void opt_encounter_set_level(int level);
void opt_encounter_toggle_shiny(bool enabled);
void opt_encounter_set_shiny_rate(int denominator);
void opt_encounter_refresh_ui();
