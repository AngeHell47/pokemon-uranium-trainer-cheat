#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Garde de compatibilite final pour le God mode. Il est enregistre apres
// opt_hp afin de rester le dernier wrapper lorsque les scripts sont charges
// tardivement par Uranium.
bool opt_godmode_repair_init();
void opt_godmode_repair_shutdown();
