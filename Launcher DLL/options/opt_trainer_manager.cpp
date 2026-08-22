#include "opt_trainer_manager.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>
#include <string.h>

namespace {

enum PendingOperation { OP_NONE = 0, OP_READ, OP_WRITE };

static CRITICAL_SECTION s_lock;
static INIT_ONCE s_once = INIT_ONCE_STATIC_INIT;
static TrainerProfile s_shared_profile = {};
static TrainerProfile s_profile = {};
static TrainerProfile s_requested = {};
static LONG s_revision = 0;
static char s_status[128] = "En attente du dresseur...";
static volatile LONG s_operation = OP_NONE;
static volatile LONG s_active = 0;

static BOOL CALLBACK initialize(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&s_lock);
    return TRUE;
}

static bool initialized() {
    return InitOnceExecuteOnce(&s_once, initialize, NULL, NULL) != FALSE;
}

static void set_status(const char* text) {
    if (!initialized()) return;
    EnterCriticalSection(&s_lock);
    lstrcpynA(s_status, text ? text : "", sizeof(s_status));
    LeaveCriticalSection(&s_lock);
}

static void ruby_escape_hex(const char* input, char* output, int capacity) {
    int written = 0;
    for (const unsigned char* current = (const unsigned char*)(input ? input : "");
         *current && written + 2 < capacity; ++current) {
        _snprintf_s(output + written, capacity - written, _TRUNCATE, "%02X", *current);
        written += 2;
    }
    output[written] = '\0';
}

static void read_profile_on_game_thread() {
    // Le transport est un bloc binaire fixe : aucune chaine Ruby issue de la
    // sauvegarde n'est injectee dans le code natif.
    const char* ruby =
        "begin\n"
        "  trainer=(defined?($Trainer) ? $Trainer : nil)\n"
        "  raise 'Dresseur indisponible.' if !trainer\n"
        "  name=(trainer.name rescue '').to_s[0,31].ljust(32,\"\\0\")\n"
        "  gender=(trainer.gender rescue 0).to_i\n"
        "  global=(defined?($PokemonGlobal) ? $PokemonGlobal : nil)\n"
        "  seconds=(global ? (global.instance_variable_get(:@playingTime) rescue 0) : 0).to_i\n"
        "  badges=(trainer.badges rescue trainer.instance_variable_get(:@badges) rescue [])\n"
        "  mask=0\n"
        "  8.times { |i| mask|=(1<<i) if (badges[i] rescue false) }\n"
        "  data=name+[gender,seconds,mask].pack('l3')\n"
        "  Win32API.new('kernel32','RtlMoveMemory',['l','p','l'],'v').call(%lu,data,%u)\n"
        "rescue Exception\n"
        "end\n";
    char code[4096] = {};
    _snprintf_s(code, sizeof(code), _TRUNCATE, ruby,
                (unsigned long)(ULONG_PTR)&s_shared_profile, (unsigned)sizeof(s_shared_profile));
    if (rgss_safe_eval(code) == 0) {
        s_shared_profile.name[sizeof(s_shared_profile.name) - 1] = '\0';
        if (s_shared_profile.gender < 0 || s_shared_profile.gender > 2)
            s_shared_profile.gender = 0;
        if (s_shared_profile.play_seconds < 0) s_shared_profile.play_seconds = 0;
        s_shared_profile.badge_mask &= 0xFF;
        EnterCriticalSection(&s_lock);
        s_profile = s_shared_profile;
        LeaveCriticalSection(&s_lock);
        InterlockedIncrement(&s_revision);
        set_status("Profil charge.");
    } else {
        set_status("Lecture du profil indisponible.");
    }
}

static void write_profile_on_game_thread() {
    TrainerProfile requested = {};
    EnterCriticalSection(&s_lock);
    requested = s_requested;
    LeaveCriticalSection(&s_lock);
    requested.name[sizeof(requested.name) - 1] = '\0';
    if (requested.gender < 0 || requested.gender > 2) requested.gender = 0;
    if (requested.play_seconds < 0) requested.play_seconds = 0;
    requested.badge_mask &= 0xFF;

    char name_hex[sizeof(requested.name) * 2 + 1] = {};
    ruby_escape_hex(requested.name, name_hex, sizeof(name_hex));
    char code[4096] = {};
    _snprintf_s(code, sizeof(code), _TRUNCATE,
        "begin\n"
        "  trainer=(defined?($Trainer) ? $Trainer : nil)\n"
        "  raise 'Dresseur indisponible.' if !trainer\n"
        "  name=['%s'].pack('H*')\n"
        "  trainer.name=name if trainer.respond_to?(:name=) && name.length>0\n"
        "  trainer.gender=%d if trainer.respond_to?(:gender=)\n"
        "  badges=(trainer.badges rescue trainer.instance_variable_get(:@badges) rescue nil)\n"
        "  if badges\n"
        "    8.times { |i| badges[i]=((%d & (1<<i))!=0) }\n"
        "  end\n"
        "  global=(defined?($PokemonGlobal) ? $PokemonGlobal : nil)\n"
        "  if global\n"
        "    global.instance_variable_set(:@playingTime,%d)\n"
        "    global.instance_variable_set(:@startTime,Time.now)\n"
        "  end\n"
        "rescue Exception\n"
        "end\n",
        name_hex, requested.gender, requested.badge_mask, requested.play_seconds);
    if (rgss_safe_eval(code) == 0) {
        set_status("Modifications appliquees. Sauvegardez le jeu pour les conserver.");
        read_profile_on_game_thread();
    } else {
        set_status("Echec de l'application du profil.");
    }
}

static void __cdecl on_game_thread_tick(void*) {
    const LONG op = InterlockedExchange(&s_operation, OP_NONE);
    if (op == OP_READ) read_profile_on_game_thread();
    else if (op == OP_WRITE) write_profile_on_game_thread();
}

} // namespace

bool opt_trainer_manager_init(const char*) {
    return initialized() && rgss_safe_dispatch_register(on_game_thread_tick, NULL);
}

void opt_trainer_manager_start() {
    InterlockedExchange(&s_active, 1);
    opt_trainer_manager_refresh();
}

void opt_trainer_manager_stop() { InterlockedExchange(&s_active, 0); }

void opt_trainer_manager_shutdown() {
    InterlockedExchange(&s_operation, OP_NONE);
    rgss_safe_dispatch_unregister(on_game_thread_tick, NULL);
}

void opt_trainer_manager_refresh() {
    if (!InterlockedExchangeAdd(&s_active, 0)) return;
    InterlockedExchange(&s_operation, OP_READ);
    rgss_safe_dispatch_notify();
}

bool opt_trainer_manager_copy_profile(TrainerProfile* out, LONG* revision,
                                      char* status, int status_capacity) {
    if (!initialized()) return false;
    EnterCriticalSection(&s_lock);
    if (out) *out = s_profile;
    if (revision) *revision = s_revision;
    if (status && status_capacity > 0) lstrcpynA(status, s_status, status_capacity);
    LeaveCriticalSection(&s_lock);
    return InterlockedExchangeAdd(&s_revision, 0) > 0;
}

void opt_trainer_manager_apply(const TrainerProfile& profile) {
    if (!initialized() || !InterlockedExchangeAdd(&s_active, 0)) return;
    EnterCriticalSection(&s_lock);
    s_requested = profile;
    LeaveCriticalSection(&s_lock);
    InterlockedExchange(&s_operation, OP_WRITE);
    rgss_safe_dispatch_notify();
}
