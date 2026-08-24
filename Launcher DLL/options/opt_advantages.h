#pragma once

#include <windows.h>

extern bool g_always_first_enabled;
extern bool g_perfect_accuracy_enabled;
extern bool g_guaranteed_critical_enabled;
extern bool g_status_immunity_enabled;
extern bool g_guaranteed_flee_enabled;
extern bool g_instant_fishing_enabled;
extern bool g_guaranteed_fishing_enabled;
extern int  g_exp_multiplier;
extern int  g_prize_money_multiplier;

void opt_advantages_init(const char* ini_path);
void opt_advantages_set_hwnd_and_start(HWND hwnd);
void opt_advantages_set_always_first(bool enabled);
void opt_advantages_set_perfect_accuracy(bool enabled);
void opt_advantages_set_guaranteed_critical(bool enabled);
void opt_advantages_set_status_immunity(bool enabled);
void opt_advantages_set_guaranteed_flee(bool enabled);
void opt_advantages_set_instant_fishing(bool enabled);
void opt_advantages_set_guaranteed_fishing(bool enabled);
void opt_advantages_set_exp_multiplier(int multiplier);
void opt_advantages_set_money_multiplier(int multiplier);
