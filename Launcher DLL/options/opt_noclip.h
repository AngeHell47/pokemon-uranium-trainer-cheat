#pragma once
#include <windows.h>

extern bool g_noclip;

void opt_noclip_init(const char* ini_path);
void opt_noclip_set_hwnd_and_start(HWND hwnd);
void opt_noclip_toggle(bool enabled);
int opt_noclip_get_hold_key();
void opt_noclip_set_hold_key(int virtual_key);
void opt_noclip_get_hold_key_name(char* buffer, int capacity);
