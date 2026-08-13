#include "../options/opt_pause.h"
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

bool g_pause_on_inactive = true;

static char s_ini[MAX_PATH];
static void* s_patch_addr = NULL;

static const unsigned char PAT[] = {
    0x83,0xBA,0x14,0x01,0x00,0x00,0x00,0x74,0x5F,0xFF,0x15
};

static void apply(bool enable) {
    unsigned char* p = (unsigned char*)s_patch_addr;
    if (!p) return;
    DWORD old;
    VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &old);
    p[0] = enable ? 0x90 : 0x74;
    p[1] = enable ? 0x90 : 0x5F;
    VirtualProtect(p, 2, old, &old);
}

static void find_patch_addr() {
    HMODULE h = GetModuleHandleA("RGSS102E.dll");
    if (!h) return;
    MODULEINFO mi = {};
    GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof mi);
    unsigned char* b = (unsigned char*)mi.lpBaseOfDll;
    for (SIZE_T i = 0; i <= mi.SizeOfImage - sizeof PAT; i++) {
        int eq = 1;
        for (int j = 0; j < (int)sizeof PAT; j++) if (b[i+j] != PAT[j]) { eq=0; break; }
        if (eq) { s_patch_addr = b+i+7; break; }
    }
}

void opt_pause_init(const char* ini_path) {
    lstrcpyA(s_ini, ini_path);
    g_pause_on_inactive = GetPrivateProfileIntA("Settings","PauseOnInactive",1,s_ini) != 0;
    find_patch_addr();
    apply(g_pause_on_inactive);
}

void opt_pause_toggle(bool enabled) {
    apply(enabled);
    WritePrivateProfileStringA("Settings","PauseOnInactive",enabled?"1":"0",s_ini);
}
