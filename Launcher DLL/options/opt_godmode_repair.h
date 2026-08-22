#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Point de compatibilite conserve pour la sequence d'initialisation du
// payload. Le God mode est installe uniquement par opt_hp.cpp.
bool opt_godmode_repair_init();
void opt_godmode_repair_shutdown();
