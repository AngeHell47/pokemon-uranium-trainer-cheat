#pragma once

#include <windows.h>

extern bool g_capture_trainers;

void opt_trainer_capture_init(const char* ini_path);
void opt_trainer_capture_set_hwnd_and_start(HWND hwnd);
void opt_trainer_capture_toggle(bool enabled);
