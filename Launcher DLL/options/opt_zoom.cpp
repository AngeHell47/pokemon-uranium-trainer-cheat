// opt_zoom.cpp — Vrai dézoom caméra pour Pokémon Uranium (RGSS1/DDraw)
//
// FONCTIONNEMENT :
// 1. Graphics.resize_screen(W*factor, H*factor) → RGSS dessine plus de terrain
// 2. On hook BitBlt (inline hook sur gdi32) pour intercepter les bandes de
//    rendu RGSS (640x8, 384x8) et les rediriger dans un bitmap mémoire
// 3. Quand une frame est complète, on fait un StretchBlt du bitmap mémoire
//    vers la surface DDraw, réduisant le gros rendu dans la petite fenêtre
// 4. On force la fenêtre à garder sa taille originale

#include "../options/opt_zoom.h"
#include "../trainer_runtime.h"
#include <string.h>
#include <stdio.h>

#pragma comment(lib, "gdi32.lib")

int g_zoom_value = 100;

static char          s_ini[MAX_PATH];
static HWND          s_game_hwnd  = NULL;
static DWORD         s_game_tid   = 0;
static volatile LONG s_pending    = 0;

static HHOOK s_hook_cwp    = NULL;
static HHOOK s_hook_getmsg = NULL;

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;

// Dimensions originales
static int s_orig_gfx_w = 0;
static int s_orig_gfx_h = 0;
static bool s_orig_saved = false;

// Taille du rendu courant (après resize_screen)
static volatile LONG s_render_w = 0;
static volatile LONG s_render_h = 0;

// Taille client de la fenêtre (ce qu'on garde fixe)
static int s_win_w = 0;
static int s_win_h = 0;

// ── Inline hook BitBlt ──────────────────────────────────────────────────────

typedef BOOL (WINAPI *BitBlt_t)(HDC, int, int, int, int, HDC, int, int, DWORD);
static BitBlt_t s_real_BitBlt = NULL;

static HDC     s_cap_dc     = NULL;
static HBITMAP s_cap_bmp    = NULL;
static int     s_cap_w      = 0;
static int     s_cap_h      = 0;

static HDC     s_frame_dst  = NULL;   // HDC destination des bandes de la frame
static int     s_frame_ymax = 0;      // Y le plus bas atteint dans la frame
static int     s_frame_band_w = 0;    // Largeur des bandes détectée
static DWORD   s_frame_rop  = 0;
static bool    s_active     = false;  // hook actif ?
static bool    s_bitblt_installed = false;

static void ensure_cap(int w, int h) {
    if (s_cap_dc && s_cap_w >= w && s_cap_h >= h) return;
    if (s_cap_bmp) DeleteObject(s_cap_bmp);
    if (s_cap_dc) DeleteDC(s_cap_dc);
    HDC scr = GetDC(NULL);
    s_cap_dc = CreateCompatibleDC(scr);
    s_cap_bmp = CreateCompatibleBitmap(scr, w, h);
    SelectObject(s_cap_dc, s_cap_bmp);
    ReleaseDC(NULL, scr);
    s_cap_w = w;
    s_cap_h = h;
}

static BOOL WINAPI hook_BitBlt(
    HDC hdcDst, int x, int y, int w, int h,
    HDC hdcSrc, int sx, int sy, DWORD rop)
{
    if (!s_active) {
        return s_real_BitBlt(hdcDst, x, y, w, h, hdcSrc, sx, sy, rop);
    }

    LONG rw = InterlockedExchangeAdd(&s_render_w, 0);
    LONG rh = InterlockedExchangeAdd(&s_render_h, 0);

    if (rw <= 0 || rh <= 0 || s_win_w <= 0 || s_win_h <= 0) {
        return s_real_BitBlt(hdcDst, x, y, w, h, hdcSrc, sx, sy, rop);
    }

    // Détecter les bandes RGSS : hauteur 8, largeur significative
    // Les bandes RGSS viennent en deux types :
    //   - Largeur = largeur de la surface (ex: 640 pour la zone principale)
    //   - sx=0, sy=0 (toujours copie depuis l'origine du HDC source)
    // On détecte si c'est une bande en vérifiant h==8 et w assez large

    if (h != 8 || w < 200 || sy != 0) {
        return s_real_BitBlt(hdcDst, x, y, w, h, hdcSrc, sx, sy, rop);
    }

    // Vérifier que ça ressemble au pattern RGSS
    // Toutes les bandes d'une frame vont vers le même HDC
    if (s_frame_dst == NULL) {
        // Première bande de la frame
        s_frame_dst = hdcDst;
        s_frame_ymax = 0;
        s_frame_band_w = w;
    } else if (s_frame_dst != hdcDst) {
        // HDC différent — c'est un autre contexte, laisser passer
        return s_real_BitBlt(hdcDst, x, y, w, h, hdcSrc, sx, sy, rop);
    }

    // Nouvelle frame ? (y revient à 0 ou décroît)
    if (y < s_frame_ymax && s_frame_ymax > rh / 2) {
        // Frame précédente terminée — flush
        if (s_cap_dc) {
            StretchBlt(
                s_frame_dst, 0, 0, s_win_w, s_win_h,
                s_cap_dc, 0, 0, (int)rw, (int)rh,
                SRCCOPY);
        }
        s_frame_ymax = 0;
    }

    // Capturer la bande dans notre bitmap
    ensure_cap((int)rw, (int)rh);
    s_real_BitBlt(s_cap_dc, x, y, w, h, hdcSrc, sx, sy, rop);

    if (y + h > s_frame_ymax)
        s_frame_ymax = y + h;

    // Frame complète ? (on a atteint le bas du rendu)
    if (s_frame_ymax >= (int)rh - 16) {
        StretchBlt(
            s_frame_dst, 0, 0, s_win_w, s_win_h,
            s_cap_dc, 0, 0, (int)rw, (int)rh,
            SRCCOPY);
        s_frame_ymax = 0;
    }

    // Ne PAS faire le BitBlt original (on a redirigé vers notre bitmap)
    return TRUE;
}

static bool hook_module_bitblt_iat(HMODULE module, FARPROC target) {
    if (!module || !target) return false;

    BYTE* base = (BYTE*)module;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;

    IMAGE_DATA_DIRECTORY imports =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress) return false;

    IMAGE_IMPORT_DESCRIPTOR* desc =
        (IMAGE_IMPORT_DESCRIPTOR*)(base + imports.VirtualAddress);
    bool patched = false;

    for (; desc->Name; ++desc) {
        const char* dll_name = (const char*)(base + desc->Name);
        if (lstrcmpiA(dll_name, "gdi32.dll") != 0) continue;

        IMAGE_THUNK_DATA32* iat =
            (IMAGE_THUNK_DATA32*)(base + desc->FirstThunk);
        for (; iat->u1.Function; ++iat) {
            if ((FARPROC)(ULONG_PTR)iat->u1.Function != target) continue;

            DWORD old = 0;
            if (!VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function),
                                PAGE_READWRITE, &old)) continue;
            iat->u1.Function = (DWORD)(ULONG_PTR)hook_BitBlt;
            VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), old, &old);
            FlushInstructionCache(GetCurrentProcess(), &iat->u1.Function,
                                  sizeof(iat->u1.Function));
            patched = true;
        }
    }
    return patched;
}

static void install_bitblt_hook() {
    if (s_bitblt_installed) return;

    HMODULE hGdi = GetModuleHandleA("gdi32.dll");
    FARPROC target = hGdi ? GetProcAddress(hGdi, "BitBlt") : NULL;
    if (!target) return;

    // Patcher uniquement les tables d'import du jeu/RGSS. L'ancien hook
    // ecrasait arbitrairement les 5 premiers octets de BitBlt et pouvait
    // couper une instruction, surtout lors d'une injection apres demarrage.
    bool patched = false;
    patched |= hook_module_bitblt_iat(GetModuleHandleA(NULL), target);
    patched |= hook_module_bitblt_iat(GetModuleHandleA("RGSS102E.dll"), target);
    if (patched) {
        s_real_BitBlt = (BitBlt_t)target;
        s_bitblt_installed = true;
    }
}

// ── RGSSEval ─────────────────────────────────────────────────────────────────

static bool resolve() {
    if (s_eval) return true;
    HMODULE h = GetModuleHandleA("RGSS102E.dll");
    if (!h) return false;
    s_eval = (RGSSEval_t)GetProcAddress(h, "RGSSEval");
    return s_eval != NULL;
}

static void post_to_game() {
    if (s_game_hwnd) PostMessageA(s_game_hwnd, WM_NULL, 0, 0);
    if (s_game_tid)  PostThreadMessageA(s_game_tid, WM_NULL, 0, 0);
}

static int s_shared[2] = {0, 0};

static void read_gfx_size() {
    if (!s_eval) return;
    char ruby[512];
    wsprintfA(ruby,
        "begin\n"
        "  w=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  w.call(%lu,[Graphics.width,Graphics.height].pack(\"ll\"),8)\n"
        "rescue Exception\n"
        "end\n",
        (ULONG_PTR)s_shared);
    s_eval(ruby);
}

static void do_resize_screen(int w, int h) {
    if (!s_eval) return;
    char ruby[256];
    wsprintfA(ruby,
        "begin\n"
        "  Graphics.resize_screen(%d,%d)\n"
        "  $game_player.center($game_player.x,$game_player.y) if $game_player\n"
        "rescue Exception\n"
        "end\n",
        w, h);
    s_eval(ruby);
}

static void force_window_size() {
    if (!s_game_hwnd || s_win_w <= 0 || s_win_h <= 0) return;
    RECT rc = {0, 0, s_win_w, s_win_h};
    DWORD style = (DWORD)GetWindowLongA(s_game_hwnd, GWL_STYLE);
    AdjustWindowRect(&rc, style, FALSE);
    SetWindowPos(s_game_hwnd, NULL, 0, 0,
        rc.right - rc.left, rc.bottom - rc.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// ── Game thread hook ─────────────────────────────────────────────────────────

static void on_game_thread_tick() {
    if (!resolve()) return;

    if (!s_orig_saved) {
        read_gfx_size();
        if (s_shared[0] > 0 && s_shared[1] > 0) {
            s_orig_gfx_w = s_shared[0];
            s_orig_gfx_h = s_shared[1];
            s_orig_saved = true;

            RECT rc;
            GetClientRect(s_game_hwnd, &rc);
            s_win_w = rc.right;
            s_win_h = rc.bottom;
        }
    }

    LONG p = InterlockedExchange(&s_pending, 0);
    if (p != 1 || !s_orig_saved) return;

    int pct = g_zoom_value;
    if (pct < 100) pct = 100;
    if (pct > 300) pct = 300;

    if (pct == 100) {
        // Désactiver le hook BitBlt
        s_active = false;
        s_frame_dst = NULL;
        s_frame_ymax = 0;

        InterlockedExchange(&s_render_w, 0);
        InterlockedExchange(&s_render_h, 0);

        // Remettre la résolution originale
        do_resize_screen(s_orig_gfx_w, s_orig_gfx_h);
        force_window_size();
    } else {
        int new_w = (s_orig_gfx_w * pct) / 100;
        int new_h = (s_orig_gfx_h * pct) / 100;
        new_w = ((new_w + 31) / 32) * 32;
        new_h = ((new_h + 31) / 32) * 32;

        // D'abord désactiver le hook pendant le changement
        s_active = false;
        s_frame_dst = NULL;
        s_frame_ymax = 0;

        // Agrandir la résolution interne
        do_resize_screen(new_w, new_h);

        // Mettre à jour les dimensions de rendu
        InterlockedExchange(&s_render_w, (LONG)new_w);
        InterlockedExchange(&s_render_h, (LONG)new_h);

        // Forcer la fenêtre à sa taille originale
        force_window_size();

        // Activer le hook BitBlt
        s_active = true;
    }
}

static LRESULT CALLBACK cwp_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) on_game_thread_tick();
    return CallNextHookEx(s_hook_cwp, code, wp, lp);
}

static LRESULT CALLBACK getmsg_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) on_game_thread_tick();
    return CallNextHookEx(s_hook_getmsg, code, wp, lp);
}

static void install_hooks() {
    if (!s_game_tid) return;
    HMODULE hSelf = g_trainer_module;
    if (!hSelf) return;
    if (!s_hook_cwp)
        s_hook_cwp = SetWindowsHookExA(WH_CALLWNDPROC, cwp_hook, hSelf, s_game_tid);
    if (!s_hook_getmsg)
        s_hook_getmsg = SetWindowsHookExA(WH_GETMESSAGE, getmsg_hook, hSelf, s_game_tid);
}

// ── API publique ──────────────────────────────────────────────────────────────

void opt_zoom_init(const char* ini_path) {
    lstrcpyA(s_ini, ini_path);
    g_zoom_value = GetPrivateProfileIntA("Settings", "CameraZoom", 100, s_ini);
    if (g_zoom_value < 100) g_zoom_value = 100;
    if (g_zoom_value > 300) g_zoom_value = 300;
    resolve();
}

void opt_zoom_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    s_game_tid = 0;
    if (hwnd) s_game_tid = GetWindowThreadProcessId(hwnd, NULL);
    install_hooks();
    install_bitblt_hook();

    if (g_zoom_value != 100) {
        InterlockedExchange(&s_pending, 1);
        post_to_game();
    }
}

void opt_zoom_apply(int percent) {
    if (percent < 100) percent = 100;
    if (percent > 300) percent = 300;
    g_zoom_value = percent;

    char buf[16];
    wsprintfA(buf, "%d", percent);
    WritePrivateProfileStringA("Settings", "CameraZoom", buf, s_ini);

    InterlockedExchange(&s_pending, 1);
    post_to_game();
}
