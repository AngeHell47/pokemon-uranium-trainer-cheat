#include "../options/opt_inventory_manager.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>
#include <string.h>

namespace {

enum InventoryCommandType {
    INVENTORY_COMMAND_SET = 1,
    INVENTORY_COMMAND_GIVE
};

struct InventoryCommand {
    LONG type;
    LONG item_id;
    LONG quantity;
    InventoryCommand* next;
};

struct InventoryShared {
    int entry_count;
    int truncated;
    int catalog_count;
    int pocket_count;
    InventoryEntry entries[INVENTORY_MANAGER_MAX_ENTRIES];
    InventoryCatalogEntry catalog[INVENTORY_MANAGER_MAX_CATALOG];
};

struct InventoryResultShared {
    int code;
    char message[124];
};

static char s_ini[MAX_PATH] = {};
static INIT_ONCE s_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION s_lock;
static CRITICAL_SECTION s_queue_lock;
static InventoryCommand* s_head = NULL;
static InventoryCommand* s_tail = NULL;

static InventoryShared s_shared = {};
static InventoryResultShared s_shared_result = {};
static InventoryEntry s_entries[INVENTORY_MANAGER_MAX_ENTRIES] = {};
static InventoryCatalogEntry s_catalog[INVENTORY_MANAGER_MAX_CATALOG] = {};
static int s_entry_count = 0;
static int s_catalog_count = 0;
static bool s_truncated = false;
static char s_status[128] = "Waiting for bag data...";
static LONG s_revision = 0;
static LONG s_status_revision = 0;

static volatile LONG s_refresh_pending = 0;
static volatile LONG s_processing = 0;
static HANDLE s_stop = NULL;
static HANDLE s_timer = NULL;

static char s_ruby_read[16384] = {};
static char s_ruby_write[6144] = {};

static BOOL CALLBACK initialize(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&s_lock);
    InitializeCriticalSection(&s_queue_lock);
    return TRUE;
}

static bool ensure_initialized() {
    return InitOnceExecuteOnce(&s_once, initialize, NULL, NULL) != FALSE;
}

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

static void set_status(const char* text) {
    if (!ensure_initialized()) return;
    EnterCriticalSection(&s_lock);
    lstrcpynA(s_status, text ? text : "", sizeof(s_status));
    InterlockedIncrement(&s_status_revision);
    LeaveCriticalSection(&s_lock);
}

static void utf8_to_ansi(char* text, int capacity) {
    if (!text || capacity <= 1) return;
    bool has_high_byte = false;
    for (const unsigned char* cursor = (const unsigned char*)text;
         *cursor; ++cursor) {
        if (*cursor >= 0x80) { has_high_byte = true; break; }
    }
    if (!has_high_byte) return;
    wchar_t wide[128] = {};
    const int wide_count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
        wide, (int)(sizeof(wide) / sizeof(wide[0])));
    if (wide_count <= 0) return;
    char converted[256] = {};
    if (WideCharToMultiByte(CP_ACP, 0, wide, -1, converted,
                            sizeof(converted), NULL, NULL) <= 0) return;
    lstrcpynA(text, converted, capacity);
}

static bool enqueue(LONG type, LONG item_id, LONG quantity) {
    if (!ensure_initialized()) return false;
    InventoryCommand* command = (InventoryCommand*)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(InventoryCommand));
    if (!command) return false;
    command->type = type;
    command->item_id = item_id;
    command->quantity = quantity;

    EnterCriticalSection(&s_queue_lock);
    if (s_tail) s_tail->next = command;
    else s_head = command;
    s_tail = command;
    LeaveCriticalSection(&s_queue_lock);
    post_to_game();
    return true;
}

static bool dequeue(InventoryCommand* out) {
    if (!out || !ensure_initialized()) return false;
    EnterCriticalSection(&s_queue_lock);
    InventoryCommand* command = s_head;
    if (command) {
        s_head = command->next;
        if (!s_head) s_tail = NULL;
    }
    LeaveCriticalSection(&s_queue_lock);
    if (!command) return false;
    *out = *command;
    out->next = NULL;
    HeapFree(GetProcessHeap(), 0, command);
    return true;
}

static void build_ruby_read() {
    _snprintf(s_ruby_read, sizeof(s_ruby_read) - 1,
        "begin\n"
        "  max_entries=%d\n"
        "  max_catalog=%d\n"
        "  entry_size=%u\n"
        "  catalog_size=%u\n"
        "  entries=[]\n"
        "  catalog=[]\n"
        "  seen_items={}\n"
        "  truncated=0\n"
        "  bag=(defined?($PokemonBag) ? $PokemonBag : nil)\n"
        "  pockets=(bag ? (bag.instance_variable_get(:@pockets) rescue []) : [])\n"
        "  pockets=[] unless pockets.is_a?(Array)\n"
        "  pockets.each_with_index do |pocket,pocket_index|\n"
        "    next unless pocket.is_a?(Array)\n"
        "    pocket.each do |entry|\n"
        "      next unless entry.is_a?(Array)\n"
        "      item_id=entry[0].to_i\n"
        "      quantity=(bag.pbQuantity(item_id) rescue entry[1] rescue 0).to_i\n"
        "      next if item_id<=0 || quantity<=0 || seen_items[item_id]\n"
        "      seen_items[item_id]=true\n"
        "      if entries.length>=max_entries\n"
        "        truncated=1\n"
        "      else\n"
        "        name=(PBItems.getName(item_id) rescue '').to_s[0,63].ljust(64,\"\\0\")\n"
        "        entries << [pocket_index,item_id,quantity].pack(\"l3\")+name\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  item_limit=(PBItems.maxValue rescue max_catalog).to_i\n"
        "  item_limit=max_catalog if item_limit<=0 || item_limit>max_catalog\n"
        "  1.upto(item_limit) do |item_id|\n"
        "    name=(PBItems.getName(item_id) rescue '').to_s\n"
        "    next if name.length==0 || name =~ /^\\?+$/\n"
        "    pocket=(pbGetPocket(item_id) rescue 0).to_i\n"
        "    if pocket<=0\n"
        "      pockets.each_with_index do |items,pocket_index|\n"
        "        next unless items.is_a?(Array)\n"
        "        if items.any? { |entry| entry.is_a?(Array) && entry[0].to_i==item_id }\n"
        "          pocket=pocket_index\n"
        "          break\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "    catalog << [item_id,pocket].pack(\"l2\")+name[0,63].ljust(64,\"\\0\")\n"
        "  end\n"
        "  catalog=catalog[0,max_catalog]\n"
        "  header=[entries.length,truncated,catalog.length,pockets.length].pack(\"l4\")\n"
        "  body=entries.join\n"
        "  body << \"\\0\"*((max_entries-entries.length)*entry_size)\n"
        "  body << catalog.join\n"
        "  body << \"\\0\"*((max_catalog-catalog.length)*catalog_size)\n"
        "  data=header+body\n"
        "  Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,data,%u)\n"
        "rescue Exception\n"
        "  begin\n"
        "    data=[0,0,0,0].pack(\"l4\")+\"\\0\"*(%u-16)\n"
        "    Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,data,%u)\n"
        "  rescue Exception\n"
        "  end\n"
        "end\n",
        INVENTORY_MANAGER_MAX_ENTRIES,
        INVENTORY_MANAGER_MAX_CATALOG,
        (unsigned)sizeof(InventoryEntry),
        (unsigned)sizeof(InventoryCatalogEntry),
        (unsigned long)(ULONG_PTR)&s_shared,
        (unsigned)sizeof(s_shared),
        (unsigned)sizeof(s_shared),
        (unsigned long)(ULONG_PTR)&s_shared,
        (unsigned)sizeof(s_shared));
    s_ruby_read[sizeof(s_ruby_read) - 1] = '\0';
}

static void build_ruby_write(const InventoryCommand& command) {
    const bool give = command.type == INVENTORY_COMMAND_GIVE;
    _snprintf(s_ruby_write, sizeof(s_ruby_write) - 1,
        "code=0\n"
        "message=\"Inventaire modifie.\"\n"
        "begin\n"
        "  bag=(defined?($PokemonBag) ? $PokemonBag : nil)\n"
        "  item_id=%d\n"
        "  amount=%d\n"
        "  maximum=(PBItems.maxValue rescue 9999).to_i\n"
        "  if !bag\n"
        "    code=-2; message=\"Aucun sac charge.\"\n"
        "  elsif item_id<=0 || item_id>maximum\n"
        "    code=-3; message=\"Objet invalide.\"\n"
        "  else\n"
        "    current=(bag.pbQuantity(item_id) rescue 0).to_i\n"
        "%s\n"
        "    actual=(bag.pbQuantity(item_id) rescue 0).to_i\n"
        "    if code==0\n"
        "      message=\"Objet #\"+item_id.to_s+\" : quantite \"+actual.to_s+\".\"\n"
        "    end\n"
        "  end\n"
        "rescue Exception => e\n"
        "  code=-1; message=(e.message rescue \"Erreur Ruby\").to_s\n"
        "ensure\n"
        "  begin\n"
        "    text=message.to_s[0,123].ljust(124,\"\\0\")\n"
        "    data=[code].pack(\"l\")+text\n"
        "    Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,data,128)\n"
        "  rescue Exception\n"
        "  end\n"
        "end\n",
        (int)command.item_id,
        (int)command.quantity,
        give ?
        "    requested=[[amount,0].max,999].min\n"
        "    if requested>0\n"
        "      if bag.respond_to?(:pbCanStore?) && !bag.pbCanStore?(item_id,requested)\n"
        "        code=-4; message=\"Pas assez de place dans cette poche.\"\n"
        "      else\n"
        "        ok=bag.pbStoreItem(item_id,requested)\n"
        "        if ok==false\n"
        "          code=-4; message=\"Ajout refuse par le sac.\"\n"
        "        end\n"
        "      end\n"
        "    end" :
        "    target=[[amount,0].max,999].min\n"
        "    if target>current\n"
        "      delta=target-current\n"
        "      if bag.respond_to?(:pbCanStore?) && !bag.pbCanStore?(item_id,delta)\n"
        "        code=-4; message=\"Pas assez de place dans cette poche.\"\n"
        "      else\n"
        "        ok=bag.pbStoreItem(item_id,delta)\n"
        "        if ok==false\n"
        "          code=-4; message=\"Quantite refusee par le sac.\"\n"
        "        end\n"
        "      end\n"
        "    elsif target<current\n"
        "      ok=bag.pbDeleteItem(item_id,current-target)\n"
        "      if ok==false\n"
        "        code=-4; message=\"Suppression refusee par le sac.\"\n"
        "      end\n"
        "    end",
        (unsigned long)(ULONG_PTR)&s_shared_result);
    s_ruby_write[sizeof(s_ruby_write) - 1] = '\0';
}

static void copy_from_shared() {
    int entries = s_shared.entry_count;
    int catalog = s_shared.catalog_count;
    if (entries < 0) entries = 0;
    if (entries > INVENTORY_MANAGER_MAX_ENTRIES)
        entries = INVENTORY_MANAGER_MAX_ENTRIES;
    if (catalog < 0) catalog = 0;
    if (catalog > INVENTORY_MANAGER_MAX_CATALOG)
        catalog = INVENTORY_MANAGER_MAX_CATALOG;

    EnterCriticalSection(&s_lock);
    s_entry_count = entries;
    s_catalog_count = catalog;
    s_truncated = s_shared.truncated != 0;
    if (entries > 0)
        memcpy(s_entries, s_shared.entries,
               (size_t)entries * sizeof(InventoryEntry));
    if (catalog > 0)
        memcpy(s_catalog, s_shared.catalog,
               (size_t)catalog * sizeof(InventoryCatalogEntry));
    for (int i = 0; i < entries; ++i) {
        s_entries[i].name[sizeof(s_entries[i].name) - 1] = '\0';
        utf8_to_ansi(s_entries[i].name, sizeof(s_entries[i].name));
    }
    for (int i = 0; i < catalog; ++i) {
        s_catalog[i].name[sizeof(s_catalog[i].name) - 1] = '\0';
        utf8_to_ansi(s_catalog[i].name, sizeof(s_catalog[i].name));
    }
    const bool first_snapshot = s_revision == 0;
    InterlockedIncrement(&s_revision);
    LeaveCriticalSection(&s_lock);
    if (first_snapshot) set_status("All pockets and the catalog are synchronized.");
}

static void update_result() {
    s_shared_result.message[sizeof(s_shared_result.message) - 1] = '\0';
    char message[128] = {};
    if (s_shared_result.code == 0) {
        lstrcpynA(message, s_shared_result.message, sizeof(message));
    } else {
        _snprintf(message, sizeof(message) - 1, "Error (%d): %s",
                  s_shared_result.code, s_shared_result.message);
        message[sizeof(message) - 1] = '\0';
    }
    set_status(message);
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedCompareExchange(&s_processing, 1, 0) != 0) return;
    bool wrote = false;
    InventoryCommand command = {};
    while (dequeue(&command)) {
        memset(&s_shared_result, 0, sizeof(s_shared_result));
        build_ruby_write(command);
        rgss_safe_eval(s_ruby_write);
        update_result();
        wrote = true;
    }
    if (wrote) InterlockedExchange(&s_refresh_pending, 1);
    if (InterlockedExchange(&s_refresh_pending, 0) != 0) {
        memset(&s_shared, 0, sizeof(s_shared));
        rgss_safe_eval(s_ruby_read);
        copy_from_shared();
    }
    InterlockedExchange(&s_processing, 0);
}

static DWORD WINAPI timer_thread(LPVOID) {
    while (WaitForSingleObject(s_stop, 900) == WAIT_TIMEOUT) {
        InterlockedExchange(&s_refresh_pending, 1);
        post_to_game();
    }
    return 0;
}

} // namespace

bool opt_inventory_manager_init(const char* ini_path) {
    if (!ensure_initialized()) return false;
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    build_ruby_read();
    s_stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!s_stop) return false;
    return rgss_safe_dispatch_register(on_game_thread_tick, NULL);
}

void opt_inventory_manager_start() {
    if (!s_stop || s_timer) return;
    ResetEvent(s_stop);
    InterlockedExchange(&s_refresh_pending, 1);
    post_to_game();
    s_timer = CreateThread(NULL, 0, timer_thread, NULL, 0, NULL);
}

void opt_inventory_manager_stop() {
    if (s_timer) {
        SetEvent(s_stop);
        WaitForSingleObject(s_timer, 2000);
        CloseHandle(s_timer);
        s_timer = NULL;
    }
}

void opt_inventory_manager_shutdown() {
    opt_inventory_manager_stop();
    rgss_safe_dispatch_unregister(on_game_thread_tick, NULL);
    if (s_stop) {
        CloseHandle(s_stop);
        s_stop = NULL;
    }
}

void opt_inventory_manager_refresh() {
    InterlockedExchange(&s_refresh_pending, 1);
    post_to_game();
}

int opt_inventory_manager_copy_entries(InventoryEntry* out, int capacity,
                                       LONG* revision, bool* truncated) {
    if (!ensure_initialized() || capacity < 0) return 0;
    EnterCriticalSection(&s_lock);
    int count = s_entry_count;
    if (count > capacity) count = capacity;
    if (out && count > 0)
        memcpy(out, s_entries, (size_t)count * sizeof(InventoryEntry));
    if (revision) *revision = s_revision;
    if (truncated) *truncated = s_truncated;
    LeaveCriticalSection(&s_lock);
    return count;
}

int opt_inventory_manager_copy_catalog(InventoryCatalogEntry* out, int capacity,
                                       LONG* revision) {
    if (!ensure_initialized() || capacity < 0) return 0;
    EnterCriticalSection(&s_lock);
    int count = s_catalog_count;
    if (count > capacity) count = capacity;
    if (out && count > 0)
        memcpy(out, s_catalog, (size_t)count * sizeof(InventoryCatalogEntry));
    if (revision) *revision = s_revision;
    LeaveCriticalSection(&s_lock);
    return count;
}

void opt_inventory_manager_copy_status(char* out, int capacity, LONG* revision) {
    if (!out || capacity <= 0 || !ensure_initialized()) return;
    EnterCriticalSection(&s_lock);
    lstrcpynA(out, s_status, capacity);
    if (revision) *revision = s_status_revision;
    LeaveCriticalSection(&s_lock);
}

void opt_inventory_manager_set_quantity(int item_id, int quantity) {
    if (item_id <= 0) return;
    if (quantity < 0) quantity = 0;
    if (quantity > 999) quantity = 999;
    if (!enqueue(INVENTORY_COMMAND_SET, item_id, quantity))
        set_status("Unable to queue the command.");
}

void opt_inventory_manager_give(int item_id, int quantity) {
    if (item_id <= 0 || quantity <= 0) return;
    if (quantity > 999) quantity = 999;
    if (!enqueue(INVENTORY_COMMAND_GIVE, item_id, quantity))
        set_status("Unable to queue the command.");
}
