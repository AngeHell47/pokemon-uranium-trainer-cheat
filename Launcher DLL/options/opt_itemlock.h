#pragma once

#include <windows.h>

extern bool g_item_lock;

void opt_itemlock_init(const char* ini_path);
void opt_itemlock_set_hwnd_and_start(HWND hwnd);
void opt_itemlock_toggle(bool enabled);
