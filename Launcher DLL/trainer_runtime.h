#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Handle exact du payload courant. Ne pas rechercher "version.dll" par nom :
// en mode trainer externe, la DLL est extraite sous un nom versionne et le
// vrai version.dll de Windows peut deja etre charge dans le processus.
extern HMODULE g_trainer_module;
