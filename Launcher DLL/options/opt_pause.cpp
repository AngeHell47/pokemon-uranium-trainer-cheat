#include "../options/opt_pause.h"
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

bool g_pause_on_inactive = true;

static char s_ini[MAX_PATH];
static void* s_patch_addr = NULL;
static unsigned char s_original[2] = {0};

static const unsigned char PAT[] = {
    0x83,0xBA,0x14,0x01,0x00,0x00,0x00,0x74,0x5F,0xFF,0x15
};

static void apply(bool enable) {
    unsigned char* p = (unsigned char*)s_patch_addr;
    if (!p) return;
    DWORD old = 0;
    if (!VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &old)) return;
    if (enable) {
        p[0] = 0x90;
        p[1] = 0x90;
    } else {
        p[0] = s_original[0];
        p[1] = s_original[1];
    }
    FlushInstructionCache(GetCurrentProcess(), p, 2);
    DWORD ignored = 0;
    VirtualProtect(p, 2, old, &ignored);
}

static void find_patch_addr() {
    HMODULE h = GetModuleHandleA("RGSS102E.dll");
    if (!h) return;
    MODULEINFO mi = {};
    if (!GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof mi) ||
        !mi.lpBaseOfDll || mi.SizeOfImage < sizeof PAT) return;
    unsigned char* b = (unsigned char*)mi.lpBaseOfDll;
    for (SIZE_T i = 0; i <= mi.SizeOfImage - sizeof PAT; i++) {
        int eq = 1;
        for (int j = 0; j < (int)sizeof PAT; j++) if (b[i+j] != PAT[j]) { eq=0; break; }
        if (eq) {
            s_patch_addr = b+i+7;
            s_original[0] = b[i+7];
            s_original[1] = b[i+8];
            break;
        }
    }
}

void opt_pause_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_pause_on_inactive = GetPrivateProfileIntA("Settings","PauseOnInactive",1,s_ini) != 0;
    find_patch_addr();
    apply(g_pause_on_inactive);
}

void opt_pause_toggle(bool enabled) {
    apply(enabled);
    WritePrivateProfileStringA("Settings","PauseOnInactive",enabled?"1":"0",s_ini);
}
