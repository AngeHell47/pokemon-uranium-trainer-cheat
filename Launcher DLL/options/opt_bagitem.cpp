#include "../options/opt_bagitem.h"
#include "../trainer_runtime.h"
#include <stdio.h>
#include <string.h>

volatile BagItemInfo g_bag_item = {0, 0, ""};
bool g_bagitem_enabled = false;

static char          s_ini[MAX_PATH];
static HWND          s_game_hwnd  = NULL;
static DWORD         s_game_tid   = 0;
static volatile LONG s_read_pending  = 0;
static HANDLE        s_timer      = NULL;
static HANDLE        s_stop       = NULL;

struct BagWriteCommand {
    LONG item_id;
    LONG quantity;
    BagWriteCommand* next;
};

static INIT_ONCE        s_write_init_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION s_write_lock;
static BagWriteCommand* s_write_head = NULL;
static BagWriteCommand* s_write_tail = NULL;
static volatile LONG    s_processing = 0;

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

static void post_to_game() {
    if (s_game_hwnd) PostMessageA(s_game_hwnd, WM_NULL, 0, 0);
    if (s_game_tid)  PostThreadMessageA(s_game_tid, WM_NULL, 0, 0);
}

static BOOL CALLBACK init_write_queue(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&s_write_lock);
    return TRUE;
}

static bool ensure_write_queue() {
    return InitOnceExecuteOnce(&s_write_init_once, init_write_queue, NULL, NULL) != FALSE;
}

static bool enqueue_write(LONG item_id, LONG quantity) {
    if (!ensure_write_queue()) return false;

    BagWriteCommand* command = (BagWriteCommand*)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BagWriteCommand));
    if (!command) return false;

    command->item_id = item_id;
    command->quantity = quantity;

    EnterCriticalSection(&s_write_lock);
    if (s_write_tail) {
        s_write_tail->next = command;
    } else {
        s_write_head = command;
    }
    s_write_tail = command;
    LeaveCriticalSection(&s_write_lock);

    post_to_game();
    return true;
}

static bool dequeue_write(BagWriteCommand* out) {
    if (!out || !ensure_write_queue()) return false;

    EnterCriticalSection(&s_write_lock);
    BagWriteCommand* command = s_write_head;
    if (command) {
        s_write_head = command->next;
        if (!s_write_head) s_write_tail = NULL;
    }
    LeaveCriticalSection(&s_write_lock);

    if (!command) return false;
    *out = *command;
    out->next = NULL;
    HeapFree(GetProcessHeap(), 0, command);
    return true;
}

// Shared buffer : [int item_id][int quantity][char name[64]] = 72 octets
static int s_shared[18];

static char s_ruby_read[4096];
static char s_ruby_write[2048];
static bool s_ruby_built = false;

static void build_ruby() {
    if (s_ruby_built) return;
    s_ruby_built = true;
    ULONG_PTR dst = (ULONG_PTR)s_shared;

    _snprintf(s_ruby_read, sizeof(s_ruby_read) - 1,
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
        "    qty=bag.pbQuantity(item_id).to_i\n"
        "    name=(PBItems.getName(item_id) rescue \"\").to_s\n"
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
    s_ruby_read[sizeof(s_ruby_read) - 1] = '\0';

    // Ruby write : modifier entry[1] directement
    // s_ruby_write est construit dynamiquement dans build_ruby_write
}

static void build_ruby_write(int item_id, int qty) {
    _snprintf(s_ruby_write, sizeof(s_ruby_write) - 1,
        "begin\n"
        "  bag=$PokemonBag\n"
        "  item_id=%d\n"
        "  if bag && item_id>0\n"
        "    current=bag.pbQuantity(item_id).to_i\n"
        "    target=%d\n"
        "    bag.pbStoreItem(item_id,target-current) if target>current\n"
        "    bag.pbDeleteItem(item_id,current-target) if target<current\n"
        "  end\n"
        "rescue Exception\n"
        "end\n",
        item_id, qty);
    s_ruby_write[sizeof(s_ruby_write) - 1] = '\0';
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
    if (!resolve()) return;
    if (InterlockedCompareExchange(&s_processing, 1, 0) != 0) return;

    bool wrote = false;
    BagWriteCommand command;
    while (dequeue_write(&command)) {
        build_ruby_write((int)command.item_id, (int)command.quantity);
        s_eval(s_ruby_write);
        wrote = true;
    }

    LONG refresh = InterlockedExchange(&s_read_pending, 0);
    if (wrote || refresh != 0) {
        s_eval(s_ruby_read);
        update_from_shared();
    }

    InterlockedExchange(&s_processing, 0);
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
        InterlockedExchange(&s_read_pending, 1);
        post_to_game();
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
    ensure_write_queue();
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

void opt_bagitem_set_quantity(int item_id, int qty) {
    if (item_id <= 0) return;
    if (qty < 0)    qty = 0;
    if (qty > 99) qty = 99;
    enqueue_write((LONG)item_id, (LONG)qty);
}
