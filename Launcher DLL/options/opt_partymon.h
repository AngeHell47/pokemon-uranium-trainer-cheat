#pragma once
#include <windows.h>

struct PartyMonInfo {
    int  valid;          // 0 = aucun Pokémon sélectionné
    int  party_index;    // -1 si inconnu
    int  species;
    int  level;
    int  hp;
    int  totalhp;
    int  gender;         // 0=male, 1=female, 2=genderless (selon Uranium)
    int  shiny;          // 0/1

    int  iv[6];          // HP, ATK, DEF, SPD, SATK, SDEF
    int  ev[6];          // HP, ATK, DEF, SPD, SATK, SDEF
    int  move_ids[4];

    char name[32];
};

extern volatile PartyMonInfo g_partymon;

void opt_partymon_init(const char* ini_path);
void opt_partymon_set_hwnd_and_start(HWND hwnd);

// écriture
void opt_partymon_set_name(const char* name);
void opt_partymon_set_level(int level);
void opt_partymon_set_gender(int gender);
void opt_partymon_set_shiny(bool shiny);
void opt_partymon_set_iv(int stat_index, int value);   // 0..5
void opt_partymon_set_ev(int stat_index, int value);   // 0..5
void opt_partymon_set_move(int slot, int move_id);     // slot 0..3

// lecture forcée
void opt_partymon_refresh_now();