#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Actions ponctuelles qui modifient la sauvegarde ou ouvrent une scene RGSS.
// Elles sont executees exclusivement par le repartiteur Graphics.update.
bool opt_extras_init(const char* ini_path);
void opt_extras_shutdown();
void opt_extras_unlock_fly_trigger();
void opt_extras_open_pc_trigger();
void opt_extras_complete_dex_trigger();
