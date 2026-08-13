#pragma once
#include <windows.h>

extern int g_money_value;

void opt_money_init(const char* ini_path);
void opt_money_set_hwnd_and_start(HWND hwnd);
void opt_money_read(void (*callback)(int));  // lire l'argent actuel depuis Ruby
void opt_money_apply(int value);
