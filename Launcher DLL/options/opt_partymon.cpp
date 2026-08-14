#include "../options/opt_partymon.h"
#include "../rgss_safe_dispatch.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

volatile PartyMonInfo g_partymon = {0};

static char          s_ini[MAX_PATH];
static HANDLE        s_timer      = NULL;
static HANDLE        s_stop       = NULL;

enum { MAXPOKENAMELENGTH = 11 };

enum EditOp {
    EDIT_NAME   = 2,
    EDIT_LEVEL  = 3,
    EDIT_GENDER = 4,
    EDIT_SHINY  = 5,
    EDIT_IV     = 6,
    EDIT_EV     = 7,
    EDIT_MOVE   = 8
};

// Une commande devient immuable des qu'elle est ajoutee a la FIFO. Les
// arguments appartiennent au noeud : deux producteurs ne peuvent donc jamais
// combiner l'operation de l'un avec les valeurs (ou le nom) de l'autre.
struct EditCommand {
    LONG op;
    LONG arg0;
    LONG arg1;
    char name[MAXPOKENAMELENGTH + 1];
    EditCommand* next;
};

static INIT_ONCE        s_edit_init_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION s_edit_lock;
static EditCommand*     s_edit_head = NULL;
static EditCommand*     s_edit_tail = NULL;
static volatile LONG    s_refresh_pending = 0;
static volatile LONG    s_processing = 0;

// 32 ints = 128 bytes
// [0] valid
// [1] party_index
// [2] species
// [3] level
// [4] hp
// [5] totalhp
// [6] gender
// [7] shiny
// [8..13] iv[6]
// [14..19] ev[6]
// [20..23] move_ids[4]
// [24..31] name[32]
static int s_shared[32];

static char s_ruby_read[16384];
static char s_ruby_write[4096];
static bool s_ruby_built = false;

static void native_debug(const char* text) {
    (void)text;
}

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

static BOOL CALLBACK init_edit_queue(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&s_edit_lock);
    return TRUE;
}

static bool ensure_edit_queue() {
    return InitOnceExecuteOnce(&s_edit_init_once, init_edit_queue, NULL, NULL) != FALSE;
}

static bool enqueue_edit(LONG op, LONG arg0, LONG arg1, const char* name) {
    if (!ensure_edit_queue()) return false;

    EditCommand* command = (EditCommand*)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(EditCommand));
    if (!command) return false;

    command->op = op;
    command->arg0 = arg0;
    command->arg1 = arg1;
    if (name) {
        lstrcpynA(command->name, name, MAXPOKENAMELENGTH + 1);
    }

    EnterCriticalSection(&s_edit_lock);
    if (s_edit_tail) {
        s_edit_tail->next = command;
    } else {
        s_edit_head = command;
    }
    s_edit_tail = command;
    LeaveCriticalSection(&s_edit_lock);

    post_to_game();
    return true;
}

static bool dequeue_edit(EditCommand* out) {
    if (!out || !ensure_edit_queue()) return false;

    EnterCriticalSection(&s_edit_lock);
    EditCommand* command = s_edit_head;
    if (command) {
        s_edit_head = command->next;
        if (!s_edit_head) s_edit_tail = NULL;
    }
    LeaveCriticalSection(&s_edit_lock);

    if (!command) return false;
    *out = *command;
    out->next = NULL;
    HeapFree(GetProcessHeap(), 0, command);
    return true;
}

static void zero_shared() {
    memset((void*)s_shared, 0, sizeof(s_shared));
    s_shared[1] = -1;
}

static void update_from_shared() {
    g_partymon.valid      = s_shared[0];
    g_partymon.party_index= s_shared[1];
    g_partymon.species    = s_shared[2];
    g_partymon.level      = s_shared[3];
    g_partymon.hp         = s_shared[4];
    g_partymon.totalhp    = s_shared[5];
    g_partymon.gender     = s_shared[6];
    g_partymon.shiny      = s_shared[7];

    for (int i = 0; i < 6; i++) g_partymon.iv[i] = s_shared[8  + i];
    for (int i = 0; i < 6; i++) g_partymon.ev[i] = s_shared[14 + i];
    for (int i = 0; i < 4; i++) g_partymon.move_ids[i] = s_shared[20 + i];

    const char* src = (const char*)&s_shared[24];
    memcpy((char*)g_partymon.name, src, MAXPOKENAMELENGTH);
    ((char*)g_partymon.name)[MAXPOKENAMELENGTH] = '\0';
}

static void ruby_escape_copy(char* dst, size_t dstsz, const char* src) {
    if (!dst || dstsz == 0) return;
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 2 < dstsz; i++) {
        char c = src[i];
        if (c == '\\' || c == '"' || c == '#') {
            if (j + 2 >= dstsz) break;
            dst[j++] = '\\';
            dst[j++] = c;
        } else if ((unsigned char)c >= 32) {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

static void build_ruby() {
    if (s_ruby_built) return;
    s_ruby_built = true;

    _snprintf(
        s_ruby_read,
        sizeof(s_ruby_read) - 1,
        "begin\n"
        "  pkmn=nil\n"
        "  if defined?($Trainer) && $Trainer && $Trainer.party && $Trainer.party.length>0\n"
        "    pkmn=$Trainer.party[0]\n"
        "  end\n"
        "  values=[0,-1]+[0]*22\n"
        "  name=''\n"
        "  if pkmn && pkmn.class.to_s==\"PokeBattle_Pokemon\"\n"
        "    iv=(pkmn.instance_variable_get(:@iv) rescue [0,0,0,0,0,0])\n"
        "    ev=(pkmn.instance_variable_get(:@ev) rescue [0,0,0,0,0,0])\n"
        "    mv=(pkmn.instance_variable_get(:@moves) rescue [])\n"
        "    values=[1,0,(pkmn.species rescue 0).to_i,(pkmn.level rescue 0).to_i,\n"
        "      (pkmn.hp rescue 0).to_i,(pkmn.totalhp rescue 0).to_i,\n"
        "      (pkmn.gender rescue 0).to_i,((pkmn.isShiny? rescue false) ? 1 : 0)]\n"
        "    6.times { |i| values << iv[i].to_i }\n"
        "    6.times { |i| values << ev[i].to_i }\n"
        "    4.times do |i|\n"
        "      move=mv[i]\n"
        "      values << ((move && move.respond_to?(:id)) ? move.id.to_i : 0)\n"
        "    end\n"
        "    name=(pkmn.name rescue '').to_s[0,11]\n"
        "  end\n"
        "  name=(name+\"\\0\"*32)[0,32]\n"
        "  data=values.pack(\"l24\")+name\n"
        "  Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,data,128)\n"
        "rescue Exception\n"
        "  begin\n"
        "    data=([0,-1]+[0]*22).pack(\"l24\")+\"\\0\"*32\n"
        "    Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,data,128)\n"
        "  rescue Exception\n"
        "  end\n"
        "end\n",
        (unsigned long)(ULONG_PTR)s_shared,
        (unsigned long)(ULONG_PTR)s_shared
    );

    s_ruby_read[sizeof(s_ruby_read) - 1] = '\0';
}

static void build_ruby_write_name(const char* escaped_name) {
    _snprintf(
        s_ruby_write,
        sizeof(s_ruby_write) - 1,
        "begin\n"
        "  pkmn=nil\n"
        "  if defined?($Trainer) && $Trainer && $Trainer.party && $Trainer.party.length>0\n"
        "    pkmn=$Trainer.party[0]\n"
        "  end\n"
        "  if pkmn\n"
        "    name=\"%s\".to_s[0,11]\n"
        "    pkmn.name = name\n"
        "    pkmn.calcStats if pkmn.respond_to?(:calcStats)\n"
        "  end\n"
        "rescue Exception\n"
        "end\n",
        escaped_name
    );
    s_ruby_write[sizeof(s_ruby_write) - 1] = '\0';
}

static void build_ruby_write_gender(int gender) {
    _snprintf(
        s_ruby_write,
        sizeof(s_ruby_write) - 1,
        "begin\n"
        "  pkmn=nil\n"
        "  if defined?($Trainer) && $Trainer && $Trainer.party && $Trainer.party.length>0\n"
        "    pkmn=$Trainer.party[0]\n"
        "  end\n"
        "  if pkmn\n"
        "    pkmn.setGender(%d) if pkmn.respond_to?(:setGender)\n"
        "    pkmn.calcStats if pkmn.respond_to?(:calcStats)\n"
        "  end\n"
        "rescue Exception\n"
        "end\n",
        gender
    );
    s_ruby_write[sizeof(s_ruby_write) - 1] = '\0';
}

static void build_ruby_write_shiny(int shiny) {
    _snprintf(
        s_ruby_write,
        sizeof(s_ruby_write) - 1,
        "begin\n"
        "  pkmn=nil\n"
        "  if defined?($Trainer) && $Trainer && $Trainer.party && $Trainer.party.length>0\n"
        "    pkmn=$Trainer.party[0]\n"
        "  end\n"
        "  if pkmn\n"
        "    if %d != 0\n"
        "      pkmn.makeShiny if pkmn.respond_to?(:makeShiny)\n"
        "    else\n"
        "      pkmn.makeNotShiny if pkmn.respond_to?(:makeNotShiny)\n"
        "    end\n"
        "    pkmn.calcStats if pkmn.respond_to?(:calcStats)\n"
        "  end\n"
        "rescue Exception\n"
        "end\n",
        shiny
    );
    s_ruby_write[sizeof(s_ruby_write) - 1] = '\0';
}

static void build_ruby_write_iv(int stat_index, int value) {
    _snprintf(
        s_ruby_write,
        sizeof(s_ruby_write) - 1,
        "begin\n"
        "  pkmn=nil\n"
        "  if defined?($Trainer) && $Trainer && $Trainer.party && $Trainer.party.length>0\n"
        "    pkmn=$Trainer.party[0]\n"
        "  end\n"
        "  if pkmn\n"
        "    arr=(pkmn.iv rescue pkmn.instance_variable_get(:@iv) rescue [0,0,0,0,0,0])\n"
        "    arr[%d]=%d\n"
        "    if pkmn.respond_to?(:iv=)\n"
        "      pkmn.iv=arr\n"
        "    else\n"
        "      pkmn.instance_variable_set(:@iv, arr)\n"
        "    end\n"
        "    pkmn.calcStats if pkmn.respond_to?(:calcStats)\n"
        "  end\n"
        "rescue Exception\n"
        "end\n",
        stat_index, value
    );
    s_ruby_write[sizeof(s_ruby_write) - 1] = '\0';
}

static void build_ruby_write_ev(int stat_index, int value) {
    _snprintf(
        s_ruby_write,
        sizeof(s_ruby_write) - 1,
        "begin\n"
        "  pkmn=nil\n"
        "  if defined?($Trainer) && $Trainer && $Trainer.party && $Trainer.party.length>0\n"
        "    pkmn=$Trainer.party[0]\n"
        "  end\n"
        "  if pkmn\n"
        "    arr=(pkmn.ev rescue pkmn.instance_variable_get(:@ev) rescue [0,0,0,0,0,0])\n"
        "    stat=%d\n"
        "    requested=%d\n"
        "    other_total=0\n"
        "    6.times { |i| other_total+=arr[i].to_i if i!=stat }\n"
        "    maximum=[255,[510-other_total,0].max].min\n"
        "    arr[stat]=[requested,maximum].min\n"
        "    if pkmn.respond_to?(:ev=)\n"
        "      pkmn.ev=arr\n"
        "    else\n"
        "      pkmn.instance_variable_set(:@ev, arr)\n"
        "    end\n"
        "    pkmn.calcStats if pkmn.respond_to?(:calcStats)\n"
        "  end\n"
        "rescue Exception\n"
        "end\n",
        stat_index, value
    );
    s_ruby_write[sizeof(s_ruby_write) - 1] = '\0';
}

static void build_ruby_write_move(int slot, int move_id) {
    _snprintf(
        s_ruby_write,
        sizeof(s_ruby_write) - 1,
        "begin\n"
        "  pkmn=nil\n"
        "  if defined?($Trainer) && $Trainer && $Trainer.party && $Trainer.party.length>0\n"
        "    pkmn=$Trainer.party[0]\n"
        "  end\n"
        "  if pkmn\n"
        "    arr=(pkmn.moves rescue pkmn.instance_variable_get(:@moves) rescue [])\n"
        "    while arr.length < 4 do arr << PBMove.new(0) end\n"
        "    arr[%d]=PBMove.new(%d)\n"
        "    if pkmn.respond_to?(:moves=)\n"
        "      pkmn.moves=arr\n"
        "    else\n"
        "      pkmn.instance_variable_set(:@moves, arr)\n"
        "    end\n"
        "    pkmn.calcStats if pkmn.respond_to?(:calcStats)\n"
        "  end\n"
        "rescue Exception\n"
        "end\n",
        slot, move_id
    );
    s_ruby_write[sizeof(s_ruby_write) - 1] = '\0';
}

static void build_ruby_write_level(int level) {
    _snprintf(
        s_ruby_write,
        sizeof(s_ruby_write) - 1,
        "begin\n"
        "  pkmn=nil\n"
        "  if defined?($Trainer) && $Trainer && $Trainer.party && $Trainer.party.length>0\n"
        "    pkmn=$Trainer.party[0]\n"
        "  end\n"
        "  if pkmn\n"
        "    lvl=%d\n"
        "    lvl=1 if lvl < 1\n"
        "    lvl=100 if lvl > 100\n"
        "    if pkmn.respond_to?(:level=)\n"
        "      pkmn.level=lvl\n"
        "    else\n"
        "      pkmn.instance_variable_set(:@level, lvl)\n"
        "    end\n"
        "    pkmn.calcStats if pkmn.respond_to?(:calcStats)\n"
        "    pkmn.heal if pkmn.respond_to?(:heal)\n"
        "  end\n"
        "rescue Exception\n"
        "end\n",
        level
    );
    s_ruby_write[sizeof(s_ruby_write) - 1] = '\0';
}

static void execute_edit(const EditCommand& command) {
    switch (command.op) {
        case EDIT_NAME: {
            char escaped[(MAXPOKENAMELENGTH * 2) + 1];
            ruby_escape_copy(escaped, sizeof(escaped), command.name);
            build_ruby_write_name(escaped);
            break;
        }
        case EDIT_LEVEL:
            build_ruby_write_level((int)command.arg0);
            break;
        case EDIT_GENDER:
            build_ruby_write_gender((int)command.arg0);
            break;
        case EDIT_SHINY:
            build_ruby_write_shiny((int)command.arg0);
            break;
        case EDIT_IV:
            build_ruby_write_iv((int)command.arg0, (int)command.arg1);
            break;
        case EDIT_EV:
            build_ruby_write_ev((int)command.arg0, (int)command.arg1);
            break;
        case EDIT_MOVE:
            build_ruby_write_move((int)command.arg0, (int)command.arg1);
            break;
        default:
            return;
    }

    rgss_safe_eval(s_ruby_write);
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedCompareExchange(&s_processing, 1, 0) != 0) return;

    bool edited = false;
    EditCommand command;
    while (dequeue_edit(&command)) {
        execute_edit(command);
        edited = true;
    }

    LONG refresh = InterlockedExchange(&s_refresh_pending, 0);
    if (edited || refresh != 0) {
        int rc = rgss_safe_eval(s_ruby_read);
        char buf[128];
        wsprintfA(buf, "read_rc=%d", rc);
        native_debug(buf);
        update_from_shared();
    }

    InterlockedExchange(&s_processing, 0);
}

static DWORD WINAPI timer_thread(LPVOID) {
    while (WaitForSingleObject(s_stop, 500) == WAIT_TIMEOUT) {
        // Le rafraichissement est coalesce dans son propre drapeau. Il ne
        // partage aucun stockage avec la FIFO des editions.
        InterlockedExchange(&s_refresh_pending, 1);
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

void opt_partymon_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    ensure_edit_queue();
    zero_shared();
    memset((void*)&g_partymon, 0, sizeof(g_partymon));
    ((PartyMonInfo&)g_partymon).party_index = -1;

    s_stop = CreateEventA(NULL, TRUE, FALSE, NULL);

    build_ruby();
	char buf[128];
    wsprintfA(buf, "ruby_read_len=%u", (unsigned)lstrlenA(s_ruby_read));
    native_debug(buf);
}

void opt_partymon_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    InterlockedExchange(&s_refresh_pending, 1);
    post_to_game();
    start_timer();

    // TEST FORCÉ
    //g_partymon.valid = 1;
    //g_partymon.party_index = 0;
    //g_partymon.species = 3;
    //g_partymon.level = 8;
    //g_partymon.hp = 49;
    //g_partymon.totalhp = 49;
    //g_partymon.gender = 0;
    //g_partymon.shiny = 1;
    //g_partymon.iv[0] = 31;
    //g_partymon.iv[1] = 31;
    //g_partymon.iv[2] = 31;
    //g_partymon.iv[3] = 31;
    //g_partymon.iv[4] = 31;
    //g_partymon.iv[5] = 31;
    //g_partymon.ev[0] = 252;
    //g_partymon.ev[1] = 252;
    //g_partymon.ev[2] = 0;
    //g_partymon.ev[3] = 0;
    //g_partymon.ev[4] = 4;
    //g_partymon.ev[5] = 0;
    //g_partymon.move_ids[0] = 278;
    //g_partymon.move_ids[1] = 368;
    //g_partymon.move_ids[2] = 146;
    //g_partymon.move_ids[3] = 245;
    //lstrcpyA((char*)g_partymon.name, "FORCED TEST");
}

void opt_partymon_set_name(const char* name) {
    enqueue_edit(EDIT_NAME, 0, 0, name ? name : "");
}

void opt_partymon_set_gender(int gender) {
    if (gender < 0) gender = 0;
    if (gender > 2) gender = 2;
    enqueue_edit(EDIT_GENDER, (LONG)gender, 0, NULL);
}

void opt_partymon_set_shiny(bool shiny) {
    enqueue_edit(EDIT_SHINY, shiny ? 1L : 0L, 0, NULL);
}

void opt_partymon_set_level(int level) {
    if (level < 1) level = 1;
    if (level > 100) level = 100;
    enqueue_edit(EDIT_LEVEL, (LONG)level, 0, NULL);
}

void opt_partymon_set_iv(int stat_index, int value) {
    if (stat_index < 0 || stat_index > 5) return;
    if (value < 0) value = 0;
    if (value > 31) value = 31;
    enqueue_edit(EDIT_IV, (LONG)stat_index, (LONG)value, NULL);
}

void opt_partymon_set_ev(int stat_index, int value) {
    if (stat_index < 0 || stat_index > 5) return;
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    enqueue_edit(EDIT_EV, (LONG)stat_index, (LONG)value, NULL);
}

void opt_partymon_set_move(int slot, int move_id) {
    if (slot < 0 || slot > 3) return;
    if (move_id < 0) move_id = 0;
    if (move_id > 9999) move_id = 9999;
    enqueue_edit(EDIT_MOVE, (LONG)slot, (LONG)move_id, NULL);
}

void opt_partymon_refresh_now() {
    InterlockedExchange(&s_refresh_pending, 1);
    post_to_game();
}
