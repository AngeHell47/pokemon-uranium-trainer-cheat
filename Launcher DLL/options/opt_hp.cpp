#include "../options/opt_hp.h"
#include "../trainer_runtime.h"

#define LOOP_MS      500
#define RESCAN_TICKS 10

bool g_hp_lock = false;

static char          s_ini[MAX_PATH];
static HANDLE        s_timer     = NULL;
static HANDLE        s_stop      = NULL;
static DWORD         s_game_tid  = 0;
static HWND          s_game_hwnd = NULL;
static volatile LONG s_pending   = 0; // 0=rien, 1=tick, 2=cleanup
static volatile LONG s_found     = 0;

// Hooks — on installe les deux, celui qui se déclenche gagne
static HHOOK s_hook_cwp   = NULL; // WH_CALLWNDPROC  (DispatchMessage)
static HHOOK s_hook_getmsg= NULL; // WH_GETMESSAGE   (PeekMessage/GetMessage)

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;

static bool resolve() {
    if (s_eval) return true;
    HMODULE h = GetModuleHandleA("RGSS102E.dll");
    if (!h) return false;
    s_eval = (RGSSEval_t)GetProcAddress(h, "RGSSEval");
    return s_eval != NULL;
}

static const char RUBY_FIND[] =
    "begin\n"
    "  $__b0=nil;$__pkmn=nil;$__found=0\n"
    "  ObjectSpace.each_object do |obj|\n"
    "    begin\n"
    "      next if $__found==1\n"
    "      arr=obj.instance_variable_get(\"@battlers\")\n"
    "      next if arr.nil?||!arr.is_a?(Array)\n"
    "      i=0\n"
    "      while i<arr.length\n"
    "        b=arr[i]; i+=1\n"
    "        next if b.nil?\n"
    "        begin\n"
    "          if b.instance_variable_get(\"@index\")==0\n"
    "            $__b0=b\n"
    "            $__pkmn=b.instance_variable_get(\"@pokemon\") rescue nil\n"
    "            $__found=1\n"
    "          end\n"
    "        rescue Exception\n"
    "        end\n"
    "      end\n"
    "    rescue Exception\n"
    "    end\n"
    "  end\n"
    "rescue Exception\n"
    "end\n";

static const char RUBY_SET[] =
    "begin\n"
    "  if $__b0.nil?\n"
    "    $__found=0\n"
    "  else\n"
    "    begin\n"
    "      $__b0.instance_variable_set(\"@hp\",999)\n"
    "      $__b0.instance_variable_set(\"@totalhp\",999)\n"
    "      $__b0.instance_variable_set(\"@oldhp\",999)\n"
    "      $__b0.instance_variable_set(\"@animhp\",999)\n"
    "      $__b0.instance_variable_set(\"@lastHP\",999)\n"
    "    rescue Exception\n"
    "      $__b0=nil;$__found=0\n"
    "    end\n"
    "    begin\n"
    "      if !$__pkmn.nil?\n"
    "        $__pkmn.instance_variable_set(\"@hp\",999)\n"
    "        $__pkmn.instance_variable_set(\"@totalhp\",999)\n"
    "        $__pkmn.instance_variable_set(\"@oldhp\",999)\n"
    "        $__pkmn.instance_variable_set(\"@animhp\",999)\n"
    "        $__pkmn.instance_variable_set(\"@lastHP\",999)\n"
    "      end\n"
    "    rescue Exception\n"
    "    end\n"
    "  end\n"
    "rescue Exception\n"
    "end\n";

static const char RUBY_CLEANUP[] =
    "begin;$__b0=nil;$__pkmn=nil;$__found=0;rescue Exception;end\n";

// ── Logique Ruby — appelée uniquement depuis les hooks (thread du jeu) ─────────
static void on_game_thread_tick() {
    if (!s_eval) return;
    LONG p = InterlockedExchange(&s_pending, 0);
    if (p == 2) {
        s_eval(RUBY_CLEANUP);
        InterlockedExchange(&s_found, 0);
    } else if (p == 1 && g_hp_lock) {
        if (InterlockedExchangeAdd(&s_found, 0) == 0) {
            s_eval(RUBY_FIND);
            InterlockedExchange(&s_found, 1);
        } else {
            s_eval(RUBY_SET);
        }
    }
}

// WH_CALLWNDPROC : déclenché quand un message est envoyé via SendMessage/DispatchMessage
static LRESULT CALLBACK cwp_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION)
        on_game_thread_tick();
    return CallNextHookEx(s_hook_cwp, code, wp, lp);
}

// WH_GETMESSAGE : déclenché quand GetMessage/PeekMessage retire un message
static LRESULT CALLBACK getmsg_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION)
        on_game_thread_tick();
    return CallNextHookEx(s_hook_getmsg, code, wp, lp);
}

static void install_hooks() {
    if (!s_game_tid) return;
    HMODULE hSelf = g_trainer_module;
    if (!s_hook_cwp)
        s_hook_cwp = SetWindowsHookExA(WH_CALLWNDPROC, cwp_hook, hSelf, s_game_tid);
    if (!s_hook_getmsg)
        s_hook_getmsg = SetWindowsHookExA(WH_GETMESSAGE, getmsg_hook, hSelf, s_game_tid);
}

// ── Timer thread ──────────────────────────────────────────────────────────────
static DWORD WINAPI timer_thread(LPVOID) {
    int ticks = 0;
    while (WaitForSingleObject(s_stop, LOOP_MS) == WAIT_TIMEOUT) {
        if (!g_hp_lock) continue;
        if (++ticks >= RESCAN_TICKS) {
            ticks = 0;
            InterlockedExchange(&s_found, 0);
        }
        InterlockedExchange(&s_pending, 1);
        // Forcer un message dans le thread du jeu pour réveiller les hooks
        if (s_game_hwnd)
            PostMessageA(s_game_hwnd, WM_NULL, 0, 0);
        if (s_game_tid)
            PostThreadMessageA(s_game_tid, WM_NULL, 0, 0);
    }
    return 0;
}

// ── Start / stop ──────────────────────────────────────────────────────────────
static void start_lock() {
    if (s_timer) return;
    resolve();
    install_hooks();
    InterlockedExchange(&s_found, 0);
    InterlockedExchange(&s_pending, 0);
    ResetEvent(s_stop);
    s_timer = CreateThread(NULL, 0, timer_thread, NULL, 0, NULL);
}

static void stop_lock() {
    if (!s_timer) return;

    // Arrêter le timer
    SetEvent(s_stop);
    WaitForSingleObject(s_timer, 3000);
    CloseHandle(s_timer);
    s_timer = NULL;

    // Demander cleanup via le hook (jamais direct depuis ce thread)
    InterlockedExchange(&s_pending, 2);
    if (s_game_hwnd) PostMessageA(s_game_hwnd, WM_NULL, 0, 0);
    if (s_game_tid)  PostThreadMessageA(s_game_tid, WM_NULL, 0, 0);

    // Attendre que le hook ait exécuté le cleanup (max 1s)
    for (int i = 0; i < 20 && InterlockedExchangeAdd(&s_pending, 0) == 2; i++)
        Sleep(50);
}

// ── API publique ──────────────────────────────────────────────────────────────
void opt_hp_set_hwnd(HWND hwnd) {
    s_game_hwnd = hwnd;
}

void opt_hp_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    if (hwnd)
        s_game_tid = GetWindowThreadProcessId(hwnd, NULL);
    install_hooks();
    if (g_hp_lock) start_lock();
}

void opt_hp_init(const char* ini_path) {
    s_stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    lstrcpyA(s_ini, ini_path);
    g_hp_lock = GetPrivateProfileIntA("Settings","HpLock",0,s_ini) != 0;
    resolve();
}

void opt_hp_toggle(bool enabled) {
    WritePrivateProfileStringA("Settings","HpLock",enabled?"1":"0",s_ini);
    if (enabled) {
        g_hp_lock = true;
        start_lock();
    } else {
        g_hp_lock = false;
        stop_lock();
    }
}
