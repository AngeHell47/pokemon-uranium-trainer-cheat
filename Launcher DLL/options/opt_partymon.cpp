#include "../options/opt_partymon.h"
#include "../trainer_runtime.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

volatile PartyMonInfo g_partymon = {0};

static char s_pm_dbg_file[MAX_PATH];
static char s_pm_file[MAX_PATH];

static char          s_ini[MAX_PATH];
static HWND          s_game_hwnd  = NULL;
static DWORD         s_game_tid   = 0;
static volatile LONG s_pending    = 0;   // 1=read,2=name,3=level,4=gender,5=shiny,6=iv,7=ev,8=move
static HANDLE        s_timer      = NULL;
static HANDLE        s_stop       = NULL;

static volatile LONG s_arg0       = 0;
static volatile LONG s_arg1       = 0;
static char          s_name_arg[64] = {0};

static HHOOK s_hook_cwp    = NULL;
static HHOOK s_hook_getmsg = NULL;

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;

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
    strncpy((char*)g_partymon.name, src, 31);
    ((char*)g_partymon.name)[31] = '\0';
}

static void ruby_escape_copy(char* dst, size_t dstsz, const char* src) {
    if (!dst || dstsz == 0) return;
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 2 < dstsz; i++) {
        char c = src[i];
        if (c == '\\' || c == '"') {
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

    char path_esc[MAX_PATH * 2];
    ruby_escape_copy(path_esc, sizeof(path_esc), s_pm_file);

    _snprintf(
        s_ruby_read,
        sizeof(s_ruby_read) - 1,
        "begin\n"
        "  pkmn=nil\n"
        "  if defined?($Trainer) && $Trainer && $Trainer.party && $Trainer.party.length>0\n"
        "    pkmn=$Trainer.party[0]\n"
        "  end\n"
        "  File.open(\"%s\",\"wb\") do |f|\n"
        "    if pkmn && pkmn.class.to_s==\"PokeBattle_Pokemon\"\n"
        "      iv=(pkmn.instance_variable_get(:@iv) rescue [0,0,0,0,0,0])\n"
        "      ev=(pkmn.instance_variable_get(:@ev) rescue [0,0,0,0,0,0])\n"
        "      mv=(pkmn.instance_variable_get(:@moves) rescue [])\n"
        "      shiny=((pkmn.isShiny? rescue false) ? 1 : 0)\n"
        "      f.puts \"valid=1\"\n"
        "      f.puts \"party_index=0\"\n"
        "      f.puts \"species=#{(pkmn.species rescue 0).to_i}\"\n"
        "      f.puts \"level=#{(pkmn.level rescue 0).to_i}\"\n"
        "      f.puts \"hp=#{(pkmn.hp rescue 0).to_i}\"\n"
        "      f.puts \"totalhp=#{(pkmn.totalhp rescue 0).to_i}\"\n"
        "      f.puts \"gender=#{(pkmn.gender rescue 0).to_i}\"\n"
        "      f.puts \"shiny=#{shiny}\"\n"
        "      f.puts \"name=#{(pkmn.name rescue '').to_s}\"\n"
        "      6.times { |i| f.puts \"iv#{i}=#{iv[i].to_i}\" }\n"
        "      6.times { |i| f.puts \"ev#{i}=#{ev[i].to_i}\" }\n"
        "      4.times do |i|\n"
        "        m=mv[i]\n"
        "        mid=(m && m.respond_to?(:id)) ? m.id.to_i : 0\n"
        "        f.puts \"move#{i}=#{mid}\"\n"
        "      end\n"
        "    else\n"
        "      f.puts \"valid=0\"\n"
        "      f.puts \"party_index=-1\"\n"
        "    end\n"
        "  end\n"
        "rescue Exception\n"
        "end\n",
        path_esc
    );

    s_ruby_read[sizeof(s_ruby_read) - 1] = '\0';
}

static void load_partymon_from_file() {
    FILE* f = fopen(s_pm_file, "rb");
    if (!f) return;

    PartyMonInfo tmp = {};
    tmp.party_index = -1;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';

        char* key = line;
        char* val = eq + 1;

        size_t n = strlen(val);
        while (n > 0 && (val[n - 1] == '\r' || val[n - 1] == '\n')) {
            val[--n] = '\0';
        }

        if      (strcmp(key, "valid") == 0)       tmp.valid = atoi(val);
        else if (strcmp(key, "party_index") == 0) tmp.party_index = atoi(val);
        else if (strcmp(key, "species") == 0)     tmp.species = atoi(val);
        else if (strcmp(key, "level") == 0)       tmp.level = atoi(val);
        else if (strcmp(key, "hp") == 0)          tmp.hp = atoi(val);
        else if (strcmp(key, "totalhp") == 0)     tmp.totalhp = atoi(val);
        else if (strcmp(key, "gender") == 0)      tmp.gender = atoi(val);
        else if (strcmp(key, "shiny") == 0)       tmp.shiny = atoi(val);
        else if (strcmp(key, "name") == 0) {
            strncpy(tmp.name, val, sizeof(tmp.name) - 1);
            tmp.name[sizeof(tmp.name) - 1] = '\0';
        }
        else if (strncmp(key, "iv", 2) == 0 && strlen(key) == 3) {
            int idx = key[2] - '0';
            if (idx >= 0 && idx < 6) tmp.iv[idx] = atoi(val);
        }
        else if (strncmp(key, "ev", 2) == 0 && strlen(key) == 3) {
            int idx = key[2] - '0';
            if (idx >= 0 && idx < 6) tmp.ev[idx] = atoi(val);
        }
        else if (strncmp(key, "move", 4) == 0 && strlen(key) == 5) {
            int idx = key[4] - '0';
            if (idx >= 0 && idx < 4) tmp.move_ids[idx] = atoi(val);
        }
    }

    fclose(f);

    g_partymon.valid = tmp.valid;
    g_partymon.party_index = tmp.party_index;
    g_partymon.species = tmp.species;
    g_partymon.level = tmp.level;
    g_partymon.hp = tmp.hp;
    g_partymon.totalhp = tmp.totalhp;
    g_partymon.gender = tmp.gender;
    g_partymon.shiny = tmp.shiny;

    for (int i = 0; i < 6; i++) g_partymon.iv[i] = tmp.iv[i];
    for (int i = 0; i < 6; i++) g_partymon.ev[i] = tmp.ev[i];
    for (int i = 0; i < 4; i++) g_partymon.move_ids[i] = tmp.move_ids[i];

    strncpy((char*)g_partymon.name, tmp.name, sizeof(tmp.name) - 1);
    ((char*)g_partymon.name)[sizeof(tmp.name) - 1] = '\0';
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
        "    pkmn.name = \"%s\"\n"
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
        "    arr[%d]=%d\n"
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

static void on_game_thread_tick() {
    if (!resolve()) return;

    LONG p = InterlockedExchange(&s_pending, 0);
    if (p == 0) return;

    if (p == 1) {
        int rc = s_eval(s_ruby_read);
        char buf[128];
        wsprintfA(buf, "read_rc=%d", rc);
        native_debug(buf);
        load_partymon_from_file();
    }
    else if (p == 2) {
        char esc[128];
        ruby_escape_copy(esc, sizeof(esc), s_name_arg);
        build_ruby_write_name(esc);
        s_eval(s_ruby_write);
        s_eval(s_ruby_read);
        load_partymon_from_file();
    }
    else if (p == 3) {
        build_ruby_write_level((int)InterlockedExchangeAdd(&s_arg0, 0));
        s_eval(s_ruby_write);
        s_eval(s_ruby_read);
        load_partymon_from_file();
    }
    else if (p == 4) {
        build_ruby_write_gender((int)InterlockedExchangeAdd(&s_arg0, 0));
        s_eval(s_ruby_write);
        s_eval(s_ruby_read);
        load_partymon_from_file();
    }
    else if (p == 5) {
        build_ruby_write_shiny((int)InterlockedExchangeAdd(&s_arg0, 0));
        s_eval(s_ruby_write);
        s_eval(s_ruby_read);
        load_partymon_from_file();
    }
    else if (p == 6) {
        build_ruby_write_iv((int)InterlockedExchangeAdd(&s_arg0, 0),
                            (int)InterlockedExchangeAdd(&s_arg1, 0));
        s_eval(s_ruby_write);
        s_eval(s_ruby_read);
        load_partymon_from_file();
    }
    else if (p == 7) {
        build_ruby_write_ev((int)InterlockedExchangeAdd(&s_arg0, 0),
                            (int)InterlockedExchangeAdd(&s_arg1, 0));
        s_eval(s_ruby_write);
        s_eval(s_ruby_read);
        load_partymon_from_file();
    }
    else if (p == 8) {
        build_ruby_write_move((int)InterlockedExchangeAdd(&s_arg0, 0),
                              (int)InterlockedExchangeAdd(&s_arg1, 0));
        s_eval(s_ruby_write);
        s_eval(s_ruby_read);
        load_partymon_from_file();
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

static DWORD WINAPI timer_thread(LPVOID) {
    while (WaitForSingleObject(s_stop, 350) == WAIT_TIMEOUT) {
        InterlockedExchange(&s_pending, 1);
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
    lstrcpyA(s_ini, ini_path);
    zero_shared();
    memset((void*)&g_partymon, 0, sizeof(g_partymon));
    ((PartyMonInfo&)g_partymon).party_index = -1;

    s_stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    resolve();

    lstrcpyA(s_pm_file, s_ini);
    char* p = strrchr(s_pm_file, '\\');
    if (p) {
        *(p + 1) = '\0';
        lstrcatA(s_pm_file, "partymon.txt");
    } else {
        lstrcpyA(s_pm_file, "partymon.txt");
    }

    lstrcpyA(s_pm_dbg_file, s_pm_file);
    char* p2 = strrchr(s_pm_dbg_file, '\\');
    if (p2) {
        *(p2 + 1) = '\0';
        lstrcatA(s_pm_dbg_file, "partymon_debug.txt");
    } else {
        lstrcpyA(s_pm_dbg_file, "partymon_debug.txt");
    }

    build_ruby();
	char buf[128];
    wsprintfA(buf, "ruby_read_len=%u", (unsigned)lstrlenA(s_ruby_read));
    native_debug(buf);
}

void opt_partymon_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    if (hwnd) s_game_tid = GetWindowThreadProcessId(hwnd, NULL);
    install_hooks();
    InterlockedExchange(&s_pending, 1);
    post_to_game();

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
    strncpy(s_name_arg, name ? name : "", sizeof(s_name_arg) - 1);
    s_name_arg[sizeof(s_name_arg) - 1] = '\0';
    InterlockedExchange(&s_pending, 2);
    post_to_game();
}

void opt_partymon_set_gender(int gender) {
    if (gender < 0) gender = 0;
    if (gender > 2) gender = 2;
    InterlockedExchange(&s_arg0, (LONG)gender);
    InterlockedExchange(&s_pending, 4);
    post_to_game();
}

void opt_partymon_set_shiny(bool shiny) {
    InterlockedExchange(&s_arg0, shiny ? 1L : 0L);
    InterlockedExchange(&s_pending, 5);
    post_to_game();
}

void opt_partymon_set_level(int level) {
    if (level < 1) level = 1;
    if (level > 100) level = 100;
    InterlockedExchange(&s_arg0, level);
    InterlockedExchange(&s_pending, 3);
    post_to_game();
}

void opt_partymon_set_iv(int stat_index, int value) {
    if (stat_index < 0 || stat_index > 5) return;
    if (value < 0) value = 0;
    if (value > 10000) value = 10000;
    InterlockedExchange(&s_arg0, (LONG)stat_index);
    InterlockedExchange(&s_arg1, (LONG)value);
    InterlockedExchange(&s_pending, 6);
    post_to_game();
}

void opt_partymon_set_ev(int stat_index, int value) {
    if (stat_index < 0 || stat_index > 5) return;
    if (value < 0) value = 0;
    if (value > 10000) value = 10000;
    InterlockedExchange(&s_arg0, (LONG)stat_index);
    InterlockedExchange(&s_arg1, (LONG)value);
    InterlockedExchange(&s_pending, 7);
    post_to_game();
}

void opt_partymon_set_move(int slot, int move_id) {
    if (slot < 0 || slot > 3) return;
    if (move_id < 0) move_id = 0;
    if (move_id > 9999) move_id = 9999;
    InterlockedExchange(&s_arg0, (LONG)slot);
    InterlockedExchange(&s_arg1, (LONG)move_id);
    InterlockedExchange(&s_pending, 8);
    post_to_game();
}

void opt_partymon_refresh_now() {
    InterlockedExchange(&s_pending, 1);
    post_to_game();
}
