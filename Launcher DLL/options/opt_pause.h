#pragma once
#include <windows.h>

extern bool g_pause_on_inactive;

void opt_pause_init(const char* ini_path);  // charge le setting
void opt_pause_toggle(bool enabled);         // appelé au toggle
