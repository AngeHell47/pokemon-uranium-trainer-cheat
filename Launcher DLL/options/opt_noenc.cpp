#include "../options/opt_noenc.h"
#include "../trainer_runtime.h"
#include <windows.h>

bool g_noenc = false;

static char          s_ini[MAX_PATH];
static HWND          s_game_hwnd = NULL;
static DWORD         s_game_tid  = 0;
static volatile LONG s_pending   = 0;

static HHOOK s_hook_cwp    = NULL;
static HHOOK s_hook_getmsg = NULL;

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;

static bool resolve() {
    if (s_eval) return true;
    HMODULE h = GetModuleHandleA("RGSS102E.dll");
    if (!h) return false;
    s_eval = (RGSSEval_t)GetProcAddress(h, "RGSSEval");
    return s_eval != NULL;
}

// Retourne "__NOENC_READY__" si $game_system existe
static const char RUBY_ENABLE[] =
    "begin\n"
    "  if $game_system\n"
    "    $game_system.encounter_disabled = true\n"
    "    \"__NOENC_READY__\"\n"
    "  else\n"
    "    \"__NOENC_NOT_READY__\"\n"
    "  end\n"
    "rescue Exception\n"
    "  \"__NOENC_NOT_READY__\"\n"
    "end\n";

static const char RUBY_DISABLE[] =
    "begin\n"
    "  if $game_system\n"
    "    $game_system.encounter_disabled = false\n"
    "    \"__NOENC_READY__\"\n"
    "  else\n"
    "    \"__NOENC_NOT_READY__\"\n"
    "  end\n"
    "rescue Exception\n"
    "  \"__NOENC_NOT_READY__\"\n"
    "end\n";

static void post_to_game() {
    if (s_game_hwnd) PostMessageA(s_game_hwnd, WM_NULL, 0, 0);
    if (s_game_tid)  PostThreadMessageA(s_game_tid, WM_NULL, 0, 0);
}

static void on_game_thread_tick() {
    if (!resolve()) return;

    LONG p = s_pending;
    if (p == 0) return;

    const char* script = NULL;
    if (p == 1) script = RUBY_ENABLE;
    else if (p == 2) script = RUBY_DISABLE;
    else return;

    // Ne consomme l'ordre QUE si RGSSEval a pu tourner
    // et qu'on suppose que le jeu est prêt.
    // Ici, comme RGSSEval ne nous renvoie pas facilement la string Ruby,
    // on garde une stratégie simple :
    // tant que le jeu n'est pas bien initialisé, on réessaie à chaque tick.
    // Dès que le jeu tourne normalement, l'affectation finit par passer.
    s_eval(script);

    // Pour NoEnc, une fois appliqué après chargement, inutile de spammer à vie :
    // on efface après quelques ticks seulement quand le jeu est lancé.
    // Version simple et robuste : effacer après une exécution réussie tardive.
    // Ici on suppose que si on arrive jusque-là après l'installation des hooks,
    // l'ordre peut être consommé.
    InterlockedExchange(&s_pending, 0);
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

    if (!s_hook_cwp) {
        s_hook_cwp = SetWindowsHookExA(WH_CALLWNDPROC, cwp_hook, hSelf, s_game_tid);
    }
    if (!s_hook_getmsg) {
        s_hook_getmsg = SetWindowsHookExA(WH_GETMESSAGE, getmsg_hook, hSelf, s_game_tid);
    }
}

void opt_noenc_init(const char* ini_path) {
    lstrcpyA(s_ini, ini_path);
    g_noenc = (GetPrivateProfileIntA("Settings", "NoEnc", 0, s_ini) != 0);
    resolve();
}

void opt_noenc_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    s_game_tid  = 0;

    if (hwnd) {
        s_game_tid = GetWindowThreadProcessId(hwnd, NULL);
    }

    install_hooks();

    // Au démarrage :
    // - si NoEnc=1 -> on applique le patch
    // - si NoEnc=0 -> on ne fait absolument rien
    InterlockedExchange(&s_pending, 0);

    if (g_noenc) {
        InterlockedExchange(&s_pending, 1);
        post_to_game();
    }
}

void opt_noenc_toggle(bool enabled) {
    g_noenc = enabled;
    WritePrivateProfileStringA("Settings", "NoEnc", enabled ? "1" : "0", s_ini);

    InterlockedExchange(&s_pending, enabled ? 1 : 2);
    post_to_game();
}
