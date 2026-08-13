#pragma once
#include <windows.h>

void opt_heal_init(const char* ini_path);
void opt_heal_set_hwnd_and_start(HWND hwnd);
void opt_heal_trigger();  // soigner toute l'équipe (appel ponctuel)
