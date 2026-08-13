#include "../options/opt_bagitem.h"
#include "../trainer_runtime.h"
#include <string.h>

volatile BagItemInfo g_bag_item = {0, 0, ""};
bool g_bagitem_enabled = false;

static char          s_ini[MAX_PATH];
static HWND          s_game_hwnd  = NULL;
static DWORD         s_game_tid   = 0;
static volatile LONG s_pending    = 0; // 1=lire, 2=écrire quantité
static volatile LONG s_write_qty  = 0;
static HANDLE        s_timer      = NULL;
static HANDLE        s_stop       = NULL;

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

// Shared buffer : [int item_id][int quantity][char name[64]] = 72 octets
static int s_shared[18];

static char s_ruby_read[1024];
static char s_ruby_write[256];
static bool s_ruby_built = false;

static void build_ruby() {
    if (s_ruby_built) return;
    s_ruby_built = true;
    ULONG_PTR dst = (ULONG_PTR)s_shared;

    wsprintfA(s_ruby_read,
        "begin\n"
        "  w=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  bag=$PokemonBag\n"
        "  pocket_idx=(bag.instance_variable_get(:@lastpocket)||0).to_i\n"
        "  choices=bag.instance_variable_get(:@choices)||[]\n"
        "  item_idx=(choices[pocket_idx]||0).to_i\n"
        "  pockets=bag.instance_variable_get(:@pockets)||[]\n"
        "  pocket=pockets[pocket_idx]\n"
        "  if pocket.is_a?(Array)&&item_idx<pocket.length&&pocket[item_idx].is_a?(Array)\n"
        "    entry=pocket[item_idx]\n"
        "    item_id=entry[0].to_i\n"
        "    qty=entry[1].to_i\n"
        "    name=\"\"\n"
        "    begin\n"
        "      lst=($ItemData.instance_variable_get(:@list) rescue nil)\n"
        "      name=lst[item_id][1].to_s if lst&&lst[item_id].is_a?(Array)\n"
        "    rescue Exception\n"
        "    end\n"
        "    name=name[0,62].ljust(64,\"\\0\")\n"
        "    buf=[item_id,qty].pack(\"ll\")+name\n"
        "    w.call(%lu,buf,72)\n"
        "  else\n"
        "    w.call(%lu,[0,0].pack(\"ll\")+\"\\0\"*64,72)\n"
        "  end\n"
        "rescue Exception\n"
        "  begin\n"
        "    w=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "    w.call(%lu,[0,0].pack(\"ll\")+\"\\0\"*64,72)\n"
        "  rescue Exception\n"
        "  end\n"
        "end\n",
        dst, dst, dst);

    // Ruby write : modifier entry[1] directement
    // s_ruby_write est construit dynamiquement dans build_ruby_write
}

static void build_ruby_write(int qty) {
    wsprintfA(s_ruby_write,
        "begin\n"
        "  bag=$PokemonBag\n"
        "  pocket_idx=(bag.instance_variable_get(:@lastpocket)||0).to_i\n"
        "  choices=bag.instance_variable_get(:@choices)||[]\n"
        "  item_idx=(choices[pocket_idx]||0).to_i\n"
        "  pockets=bag.instance_variable_get(:@pockets)||[]\n"
        "  pocket=pockets[pocket_idx]\n"
        "  if pocket.is_a?(Array)&&item_idx<pocket.length&&pocket[item_idx].is_a?(Array)\n"
        "    pocket[item_idx][1]=%d\n"
        "  end\n"
        "rescue Exception\n"
        "end\n",
        qty);
}

static void update_from_shared() {
    g_bag_item.item_id  = s_shared[0];
    g_bag_item.quantity = s_shared[1];
    char* src = (char*)&s_shared[2];
    strncpy((char*)g_bag_item.name, src, 63);
    ((char*)g_bag_item.name)[63] = '\0';
}

// ── Hook : exécuté dans le thread du jeu ─────────────────────────────────────
static void on_game_thread_tick() {
    if (!s_eval) return;
    LONG p = InterlockedExchange(&s_pending, 0);
    if (p == 0) return;
    if (p == 2) {
        LONG qty = InterlockedExchangeAdd(&s_write_qty, 0);
        build_ruby_write((int)qty);
        s_eval(s_ruby_write);
        // Relire après écriture
        s_eval(s_ruby_read);
        update_from_shared();
    } else if (p == 1) {
        s_eval(s_ruby_read);
        update_from_shared();
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
    if (!s_hook_cwp)
        s_hook_cwp = SetWindowsHookExA(WH_CALLWNDPROC, cwp_hook, hSelf, s_game_tid);
    if (!s_hook_getmsg)
        s_hook_getmsg = SetWindowsHookExA(WH_GETMESSAGE, getmsg_hook, hSelf, s_game_tid);
}

// ── Timer thread : pose pending=1 toutes les 500ms ────────────────────────────
static DWORD WINAPI timer_thread(LPVOID) {
    while (WaitForSingleObject(s_stop, 500) == WAIT_TIMEOUT) {
        InterlockedExchange(&s_pending, 1);
        if (s_game_hwnd) PostMessageA(s_game_hwnd, WM_NULL, 0, 0);
        if (s_game_tid)  PostThreadMessageA(s_game_tid, WM_NULL, 0, 0);
    }
    return 0;
}

static void start_timer() {
    if (s_timer) return;
    ResetEvent(s_stop);
    s_timer = CreateThread(NULL, 0, timer_thread, NULL, 0, NULL);
}

static void stop_timer() {
    if (!s_timer) return;
    SetEvent(s_stop);
    WaitForSingleObject(s_timer, 2000);
    CloseHandle(s_timer);
    s_timer = NULL;
}

void opt_bagitem_init(const char* ini_path) {
    lstrcpyA(s_ini, ini_path);
    g_bagitem_enabled = true;
    s_stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    memset(s_shared, 0, sizeof s_shared);
    resolve();
    build_ruby();
}

void opt_bagitem_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    if (hwnd) s_game_tid = GetWindowThreadProcessId(hwnd, NULL);
    install_hooks();
    start_timer();
}

void opt_bagitem_set_quantity(int qty) {
    if (qty < 0)    qty = 0;
    if (qty > 9999) qty = 9999;
    InterlockedExchange(&s_write_qty, (LONG)qty);
    InterlockedExchange(&s_pending, 2);
    if (s_game_hwnd) PostMessageA(s_game_hwnd, WM_NULL, 0, 0);
    if (s_game_tid)  PostThreadMessageA(s_game_tid, WM_NULL, 0, 0);
}
