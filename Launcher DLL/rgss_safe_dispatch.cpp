#include "rgss_safe_dispatch.h"

#include <psapi.h>
#include <string.h>

#if !defined(_M_IX86)
#error rgss_safe_dispatch requires the 32-bit RGSS process ABI
#endif

namespace {

typedef unsigned long RubyValue;
typedef RubyValue (__cdecl *GraphicsUpdate_t)(RubyValue self);
typedef int (__cdecl *RGSSEval_t)(const char* script);

struct CallbackEntry {
    RgssSafeCallback callback;
    void* context;
};

struct CallbackBarrier {
    HANDLE complete;
};

enum DispatchState {
    DISPATCH_STOPPED = 0,
    DISPATCH_BOOTSTRAP = 1,
    DISPATCH_PATCHING = 2,
    DISPATCH_RUNNING = 3,
    DISPATCH_STOPPING = 4,
    DISPATCH_FAILED = -1
};

static const DWORD kRgssImageSize = 0x1F8000;
static const DWORD kRgssTimestamp = 0x4AE11007;
static const DWORD kGraphicsUpdateRva = 0xC6F0;
static const DWORD kGraphicsRegistrationRva = 0xC55D;
static const DWORD kEvalRva = 0x5C50;
static const size_t kPatchLength = 8;
static const size_t kMaxCallbacks = 48;
static const DWORD kHookGraceMs = 250;
static const LONG kMaxSuspendAttempts = 64;

struct PreparedInstall {
    BYTE* target;
    RGSSEval_t eval;
    BYTE original[kPatchLength];
    BYTE patch[kPatchLength];
    BYTE* trampoline;
    LONG64 original64;
    LONG64 patch64;
    DWORD old_target_protect;
    bool target_writable;
};

static CRITICAL_SECTION s_lock;
// 0=absente, 1=initialisation en cours, 2=publiee.
static volatile LONG s_lock_ready = 0;
static CallbackEntry s_callbacks[kMaxCallbacks] = {};
static size_t s_callback_count = 0;
static volatile LONG s_snapshot_active = 0;
static HANDLE s_snapshot_idle = NULL;

static HMODULE s_owner = NULL;
static HWND s_game = NULL;
static DWORD s_game_tid = 0;
static DWORD s_owner_tid = 0;
static HHOOK s_cwp_hook = NULL;
static HHOOK s_getmsg_hook = NULL;
static HANDLE s_ready = NULL;
static HANDLE s_stop_complete = NULL;
static volatile LONG s_state = DISPATCH_STOPPED;
static volatile LONG s_in_callbacks = 0;
static volatile LONG s_detour_depth = 0;
static volatile LONG s_stop_detached = 0;
static volatile LONG s_frame_serial = 0;
static volatile LONG s_patch_owned = 0;
static volatile LONG s_diag_stage = RGSS_DISPATCH_STAGE_IDLE;
static volatile LONG s_diag_error = ERROR_SUCCESS;
static volatile LONG s_diag_eip = 0;
static volatile LONG s_diag_attempts = 0;
static volatile LONG s_diag_game_tid = 0;

static BYTE* s_target = NULL;
static BYTE s_original[kPatchLength] = {};
static GraphicsUpdate_t s_trampoline = NULL;
static RGSSEval_t s_eval = NULL;

static RubyValue __cdecl graphics_update_detour(RubyValue self);
static void remove_bootstrap_hooks();

static void __cdecl signal_callback_barrier(void* context) {
    CallbackBarrier* barrier = (CallbackBarrier*)context;
    if (barrier && barrier->complete) SetEvent(barrier->complete);
}

static void set_diagnostic_stage(LONG stage) {
    InterlockedExchange(&s_diag_stage, stage);
}

static void set_diagnostic_error(DWORD error) {
    InterlockedExchange(&s_diag_error, (LONG)error);
}

static void set_diagnostic_eip(DWORD eip) {
    InterlockedExchange(&s_diag_eip, (LONG)eip);
}

static DWORD elapsed_since(DWORD start) {
    return GetTickCount() - start;
}

static DWORD remaining_timeout(DWORD start, DWORD timeout) {
    if (timeout == INFINITE) return INFINITE;
    const DWORD elapsed = elapsed_since(start);
    return elapsed >= timeout ? 0 : timeout - elapsed;
}

static bool read_exact(const void* address, void* destination, SIZE_T size) {
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, destination, size,
                             &read) && read == size;
}

static bool validate_rgss(HMODULE* module_out, BYTE** graphics_out,
                          RGSSEval_t* eval_out) {
    HMODULE module = GetModuleHandleA("RGSS102E.dll");
    if (!module) return false;

    MODULEINFO info = {};
    if (!GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)) ||
        info.SizeOfImage != kRgssImageSize)
        return false;

    const BYTE* const base = (const BYTE*)module;
    const IMAGE_DOS_HEADER* const dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 ||
        (DWORD)dos->e_lfanew > info.SizeOfImage - sizeof(IMAGE_NT_HEADERS32))
        return false;

    const IMAGE_NT_HEADERS32* const nt =
        (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->FileHeader.TimeDateStamp != kRgssTimestamp)
        return false;

    BYTE graphics[14] = {};
    if (!read_exact(base + kGraphicsUpdateRva, graphics, sizeof(graphics)))
        return false;
    DWORD pushed_address = 0;
    DWORD call_slot = 0;
    memcpy(&pushed_address, graphics + 4, sizeof(pushed_address));
    memcpy(&call_slot, graphics + 10, sizeof(call_slot));
    if (graphics[0] != 0x55 || graphics[1] != 0x8B ||
        graphics[2] != 0xEC || graphics[3] != 0x68 ||
        graphics[8] != 0xFF || graphics[9] != 0x15 ||
        pushed_address != (DWORD)(ULONG_PTR)(base + 0x16D628) ||
        call_slot != (DWORD)(ULONG_PTR)(base + 0x126164))
        return false;

    // Valider que le callback est effectivement enregistre comme methode
    // singleton Graphics.update d'arite zero sur cette build precise.
    BYTE registration[27] = {};
    if (!read_exact(base + kGraphicsRegistrationRva, registration,
                    sizeof(registration)))
        return false;
    DWORD registered_callback = 0;
    DWORD registered_name = 0;
    DWORD graphics_global = 0;
    LONG define_delta = 0;
    memcpy(&registered_callback, registration + 3, 4);
    memcpy(&registered_name, registration + 8, 4);
    memcpy(&graphics_global, registration + 14, 4);
    memcpy(&define_delta, registration + 20, 4);
    const BYTE* const define_target =
        base + kGraphicsRegistrationRva + 24 + define_delta;
    const BYTE registration_prefix[] = {0x6A, 0x00, 0x68};
    if (memcmp(registration, registration_prefix,
               sizeof(registration_prefix)) != 0 ||
        registration[7] != 0x68 || registration[12] != 0x8B ||
        registration[13] != 0x0D || registration[18] != 0x51 ||
        registration[19] != 0xE8 || registration[24] != 0x83 ||
        registration[25] != 0xC4 || registration[26] != 0x10 ||
        registered_callback !=
            (DWORD)(ULONG_PTR)(base + kGraphicsUpdateRva) ||
        registered_name != (DWORD)(ULONG_PTR)(base + 0x12E074) ||
        graphics_global != (DWORD)(ULONG_PTR)(base + 0x16CBEC) ||
        define_target != base + 0x34450 ||
        memcmp(base + 0x12E074, "update", 7) != 0)
        return false;

    const BYTE expected_eval_prefix[] = {
        0x55, 0x8B, 0xEC, 0x51, 0xC7, 0x45, 0xFC, 0x00,
        0x00, 0x00, 0x00, 0x8D, 0x45, 0xFC, 0x50, 0x8B,
        0x4D, 0x08, 0x51, 0xE8
    };
    BYTE eval_prologue[24] = {};
    if (!read_exact(base + kEvalRva, eval_prologue, sizeof(eval_prologue)) ||
        memcmp(eval_prologue, expected_eval_prefix,
               sizeof(expected_eval_prefix)) != 0)
        return false;
    LONG eval_delta = 0;
    memcpy(&eval_delta, eval_prologue + 20, sizeof(eval_delta));
    if (base + kEvalRva + sizeof(eval_prologue) + eval_delta !=
        base + 0x3D230)
        return false;

    FARPROC exported_eval = GetProcAddress(module, "RGSSEval");
    if (!exported_eval ||
        exported_eval != (FARPROC)(base + kEvalRva))
        return false;

    *module_out = module;
    *graphics_out = (BYTE*)base + kGraphicsUpdateRva;
    *eval_out = (RGSSEval_t)exported_eval;
    return true;
}

static DWORD last_error_or(DWORD fallback) {
    const DWORD error = GetLastError();
    return error != ERROR_SUCCESS ? error : fallback;
}

static LONG64 load_long64(const BYTE* bytes) {
    LONG64 value = 0;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static LONG64 atomic_replace_8(BYTE* target, LONG64 expected,
                               LONG64 replacement) {
    return InterlockedCompareExchange64((volatile LONG64*)target,
                                        replacement, expected);
}

static void restore_target_protection(PreparedInstall* prepared) {
    if (!prepared || !prepared->target_writable || !prepared->target) return;
    DWORD ignored = 0;
    if (!VirtualProtect(prepared->target, kPatchLength,
                        prepared->old_target_protect, &ignored) &&
        InterlockedCompareExchange(&s_diag_error, 0, 0) == ERROR_SUCCESS) {
        set_diagnostic_error(last_error_or(ERROR_ACCESS_DENIED));
    }
    prepared->target_writable = false;
}

static void unpublish_prepared(const PreparedInstall* prepared) {
    if (!prepared || s_trampoline !=
        (GraphicsUpdate_t)prepared->trampoline) return;
    s_target = NULL;
    s_eval = NULL;
    s_trampoline = NULL;
    s_owner_tid = 0;
    ZeroMemory(s_original, sizeof(s_original));
}

static void release_prepared(PreparedInstall* prepared, bool unpublish) {
    if (!prepared) return;
    restore_target_protection(prepared);
    if (unpublish) unpublish_prepared(prepared);
    if (prepared->trampoline) {
        VirtualFree(prepared->trampoline, 0, MEM_RELEASE);
        prepared->trampoline = NULL;
    }
}

static bool fail_prepared_install(PreparedInstall* prepared, DWORD error,
                                  bool keep_owned_patch) {
    set_diagnostic_error(error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE);
    set_diagnostic_stage(RGSS_DISPATCH_STAGE_FAILED);
    if (!keep_owned_patch) {
        InterlockedExchange(&s_patch_owned, 0);
        release_prepared(prepared, true);
    } else {
        restore_target_protection(prepared);
        if (prepared) prepared->trampoline = NULL;
    }
    InterlockedExchange(&s_state, DISPATCH_FAILED);
    if (s_ready) SetEvent(s_ready);
    return false;
}

static bool prepare_claimed_install(PreparedInstall* prepared) {
    if (!prepared) return false;
    ZeroMemory(prepared, sizeof(*prepared));
    set_diagnostic_stage(RGSS_DISPATCH_STAGE_VALIDATING);

    HMODULE rgss = NULL;
    if (!validate_rgss(&rgss, &prepared->target, &prepared->eval)) {
        set_diagnostic_error(ERROR_BAD_EXE_FORMAT);
        return false;
    }
    if (((ULONG_PTR)prepared->target & 7u) != 0) {
        set_diagnostic_error(ERROR_INVALID_ADDRESS);
        return false;
    }
    if (!read_exact(prepared->target, prepared->original,
                    sizeof(prepared->original))) {
        set_diagnostic_error(last_error_or(ERROR_READ_FAULT));
        return false;
    }

    BYTE expected[kPatchLength] = {0x55, 0x8B, 0xEC, 0x68, 0, 0, 0, 0};
    const DWORD pushed_address =
        (DWORD)(ULONG_PTR)((BYTE*)rgss + 0x16D628);
    memcpy(expected + 4, &pushed_address, sizeof(pushed_address));
    if (memcmp(prepared->original, expected, sizeof(expected)) != 0) {
        set_diagnostic_error(ERROR_INVALID_DATA);
        return false;
    }

    prepared->trampoline = (BYTE*)VirtualAlloc(
        NULL, 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!prepared->trampoline) {
        set_diagnostic_error(last_error_or(ERROR_NOT_ENOUGH_MEMORY));
        return false;
    }
    memcpy(prepared->trampoline, prepared->original, kPatchLength);
    prepared->trampoline[8] = 0xE9;
    const DWORD return_delta =
        (DWORD)(ULONG_PTR)(prepared->target + kPatchLength) -
        (DWORD)(ULONG_PTR)(prepared->trampoline + 13);
    memcpy(prepared->trampoline + 9, &return_delta, sizeof(return_delta));
    DWORD old_trampoline_protect = 0;
    if (!VirtualProtect(prepared->trampoline, 13, PAGE_EXECUTE_READ,
                        &old_trampoline_protect) ||
        !FlushInstructionCache(GetCurrentProcess(), prepared->trampoline, 13)) {
        set_diagnostic_error(last_error_or(ERROR_WRITE_FAULT));
        VirtualFree(prepared->trampoline, 0, MEM_RELEASE);
        prepared->trampoline = NULL;
        return false;
    }

    const BYTE patch[kPatchLength] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
    memcpy(prepared->patch, patch, sizeof(patch));
    const DWORD detour_delta =
        (DWORD)(ULONG_PTR)&graphics_update_detour -
        (DWORD)(ULONG_PTR)(prepared->target + 5);
    memcpy(prepared->patch + 1, &detour_delta, sizeof(detour_delta));
    prepared->original64 = load_long64(prepared->original);
    prepared->patch64 = load_long64(prepared->patch);
    set_diagnostic_stage(RGSS_DISPATCH_STAGE_TRAMPOLINE_READY);
    return true;
}

static bool make_target_writable(PreparedInstall* prepared) {
    if (!VirtualProtect(prepared->target, kPatchLength,
                        PAGE_EXECUTE_READWRITE,
                        &prepared->old_target_protect)) {
        set_diagnostic_error(last_error_or(ERROR_ACCESS_DENIED));
        return false;
    }
    prepared->target_writable = true;
    set_diagnostic_stage(RGSS_DISPATCH_STAGE_TARGET_WRITABLE);
    return true;
}

static void publish_prepared(const PreparedInstall* prepared) {
    memcpy(s_original, prepared->original, sizeof(s_original));
    s_target = prepared->target;
    s_eval = prepared->eval;
    s_trampoline = (GraphicsUpdate_t)prepared->trampoline;
    // Le worker de bootstrap n'est jamais le proprietaire RGSS.
    s_owner_tid = s_game_tid;
}

static bool rollback_committed_patch(PreparedInstall* prepared) {
    if (atomic_replace_8(prepared->target, prepared->patch64,
                         prepared->original64) != prepared->patch64)
        return false;
    if (!FlushInstructionCache(GetCurrentProcess(), prepared->target,
                               kPatchLength))
        return false;
    InterlockedExchange(&s_patch_owned, 0);
    return true;
}

static bool commit_on_game_thread(PreparedInstall* prepared) {
    if (!make_target_writable(prepared))
        return fail_prepared_install(
            prepared, InterlockedCompareExchange(&s_diag_error, 0, 0), false);

    publish_prepared(prepared);
    set_diagnostic_stage(RGSS_DISPATCH_STAGE_COMMITTING);
    if (atomic_replace_8(prepared->target, prepared->original64,
                         prepared->patch64) != prepared->original64) {
        return fail_prepared_install(prepared, ERROR_INVALID_DATA, false);
    }
    InterlockedExchange(&s_patch_owned, 1);
    if (!FlushInstructionCache(GetCurrentProcess(), prepared->target,
                               kPatchLength)) {
        const DWORD error = last_error_or(ERROR_WRITE_FAULT);
        const bool rolled_back = rollback_committed_patch(prepared);
        return fail_prepared_install(prepared, error, !rolled_back);
    }

    InterlockedExchange(&s_state, DISPATCH_RUNNING);
    set_diagnostic_error(ERROR_SUCCESS);
    set_diagnostic_stage(RGSS_DISPATCH_STAGE_RUNNING);
    restore_target_protection(prepared);
    prepared->trampoline = NULL;
    if (s_ready) SetEvent(s_ready);
    return true;
}

static bool resume_acquired_suspend(HANDLE thread) {
    if (ResumeThread(thread) != (DWORD)-1) return true;
    const DWORD first_error = last_error_or(ERROR_GEN_FAILURE);
    // Un echec ne decremente pas le compteur. Un seul retry est donc permis.
    if (ResumeThread(thread) != (DWORD)-1) return true;
    set_diagnostic_error(first_error);
    return false;
}

static bool commit_with_suspended_game_thread(PreparedInstall* prepared,
                                               DWORD retry_budget_ms) {
    DWORD process_id = 0;
    const DWORD current_tid = GetWindowThreadProcessId(s_game, &process_id);
    if (!s_game || current_tid != s_game_tid ||
        process_id != GetCurrentProcessId())
        return fail_prepared_install(prepared, ERROR_INVALID_WINDOW_HANDLE,
                                     false);

    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                               THREAD_QUERY_INFORMATION, FALSE, s_game_tid);
    if (!thread)
        return fail_prepared_install(
            prepared, last_error_or(ERROR_ACCESS_DENIED), false);
    if (!make_target_writable(prepared)) {
        const DWORD error = InterlockedCompareExchange(&s_diag_error, 0, 0);
        CloseHandle(thread);
        return fail_prepared_install(prepared, error, false);
    }

    const DWORD retry_started = GetTickCount();
    DWORD failure = ERROR_BUSY;
    for (LONG local_attempt = 0; local_attempt < kMaxSuspendAttempts;
         ++local_attempt) {
        if (local_attempt > 0 && retry_budget_ms != INFINITE &&
            elapsed_since(retry_started) >= retry_budget_ms)
            break;

        set_diagnostic_stage(RGSS_DISPATCH_STAGE_SUSPENDING);
        InterlockedIncrement(&s_diag_attempts);
        const DWORD previous_suspend = SuspendThread(thread);
        if (previous_suspend == (DWORD)-1) {
            failure = last_error_or(ERROR_ACCESS_DENIED);
            break;
        }

        CONTEXT context = {};
        context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(thread, &context)) {
            failure = last_error_or(ERROR_INVALID_DATA);
            if (!resume_acquired_suspend(thread))
                failure = InterlockedCompareExchange(&s_diag_error, 0, 0);
            break;
        }
        set_diagnostic_eip(context.Eip);

        // Ne jamais modifier un compteur de suspension appartenant a un
        // debugger ou a un autre composant. On annule seulement notre +1.
        if (previous_suspend != 0) {
            failure = ERROR_BUSY;
            if (!resume_acquired_suspend(thread))
                failure = InterlockedCompareExchange(&s_diag_error, 0, 0);
            break;
        }

        const DWORD begin = (DWORD)(ULONG_PTR)prepared->target;
        if (context.Eip >= begin &&
            context.Eip < begin + (DWORD)kPatchLength) {
            set_diagnostic_stage(RGSS_DISPATCH_STAGE_EIP_BUSY);
            set_diagnostic_error(ERROR_BUSY);
            if (!resume_acquired_suspend(thread)) {
                failure = InterlockedCompareExchange(&s_diag_error, 0, 0);
                break;
            }
            Sleep(1); // Jamais attendre tant que le thread est suspendu.
            continue;
        }

        publish_prepared(prepared);
        set_diagnostic_stage(RGSS_DISPATCH_STAGE_COMMITTING);
        if (atomic_replace_8(prepared->target, prepared->original64,
                             prepared->patch64) != prepared->original64) {
            failure = ERROR_INVALID_DATA;
            if (!resume_acquired_suspend(thread))
                failure = (DWORD)InterlockedCompareExchange(
                    &s_diag_error, 0, 0);
            break;
        }
        InterlockedExchange(&s_patch_owned, 1);

        if (!FlushInstructionCache(GetCurrentProcess(), prepared->target,
                                   kPatchLength)) {
            failure = last_error_or(ERROR_WRITE_FAULT);
            const bool rolled_back = rollback_committed_patch(prepared);
            resume_acquired_suspend(thread);
            CloseHandle(thread);
            return fail_prepared_install(prepared, failure, !rolled_back);
        }

        // Tous les pointeurs sont publies avant le CAS64 (barriere complete).
        // RUNNING avant Resume empeche le premier frame de voir un demi-etat.
        InterlockedExchange(&s_state, DISPATCH_RUNNING);
        set_diagnostic_error(ERROR_SUCCESS);
        set_diagnostic_stage(RGSS_DISPATCH_STAGE_RESUMING);
        if (!resume_acquired_suspend(thread)) {
            failure = InterlockedCompareExchange(&s_diag_error, 0, 0);
            InterlockedExchange(&s_state, DISPATCH_PATCHING);
            const bool rolled_back = rollback_committed_patch(prepared);
            // Le premier Resume a echoue sans decrementer; retenter apres
            // rollback evite de laisser le jeu suspendu sur un chemin d'erreur.
            resume_acquired_suspend(thread);
            CloseHandle(thread);
            return fail_prepared_install(prepared, failure, !rolled_back);
        }

        CloseHandle(thread);
        restore_target_protection(prepared);
        prepared->trampoline = NULL;
        set_diagnostic_stage(RGSS_DISPATCH_STAGE_RUNNING);
        if (s_ready) SetEvent(s_ready);
        return true;
    }

    CloseHandle(thread);
    return fail_prepared_install(prepared, failure, false);
}

static bool install_on_game_thread() {
    if (GetCurrentThreadId() != s_game_tid) return false;
    const LONG previous = InterlockedCompareExchange(
        &s_state, DISPATCH_PATCHING, DISPATCH_BOOTSTRAP);
    if (previous == DISPATCH_RUNNING) return true;
    if (previous != DISPATCH_BOOTSTRAP) return false;
    set_diagnostic_stage(RGSS_DISPATCH_STAGE_HOOK_CLAIMED);

    PreparedInstall prepared = {};
    if (!prepare_claimed_install(&prepared)) {
        const DWORD error = InterlockedCompareExchange(&s_diag_error, 0, 0);
        return fail_prepared_install(&prepared, error, false);
    }
    return commit_on_game_thread(&prepared);
}

// Fallback borne pour Uranium inactif : le worker gagne le meme CAS que les
// hooks, retire ceux-ci avant toute suspension, puis ne touche au prologue que
// si le contexte x86 est hors de la fenetre de 8 octets.
static bool install_with_suspended_game_thread(DWORD retry_budget_ms) {
    const LONG previous = InterlockedCompareExchange(
        &s_state, DISPATCH_PATCHING, DISPATCH_BOOTSTRAP);
    if (previous == DISPATCH_RUNNING) return true;
    if (previous != DISPATCH_BOOTSTRAP) return false;
    set_diagnostic_stage(RGSS_DISPATCH_STAGE_WORKER_CLAIMED);

    // UnhookWindowsHookEx peut attendre un callback en vol : toujours le faire
    // avant SuspendThread.
    remove_bootstrap_hooks();

    PreparedInstall prepared = {};
    if (!prepare_claimed_install(&prepared)) {
        const DWORD error = InterlockedCompareExchange(&s_diag_error, 0, 0);
        return fail_prepared_install(&prepared, error, false);
    }
    return commit_with_suspended_game_thread(&prepared, retry_budget_ms);
}

static void bootstrap_tick() {
    if (InterlockedCompareExchange(&s_state, DISPATCH_BOOTSTRAP,
                                   DISPATCH_BOOTSTRAP) == DISPATCH_BOOTSTRAP)
        install_on_game_thread();
}

static LRESULT CALLBACK cwp_hook(int code, WPARAM wparam, LPARAM lparam) {
    HHOOK hook = s_cwp_hook;
    if (code == HC_ACTION) bootstrap_tick();
    return CallNextHookEx(hook, code, wparam, lparam);
}

static LRESULT CALLBACK getmsg_hook(int code, WPARAM wparam, LPARAM lparam) {
    HHOOK hook = s_getmsg_hook;
    if (code == HC_ACTION) bootstrap_tick();
    return CallNextHookEx(hook, code, wparam, lparam);
}

static void remove_bootstrap_hooks() {
    HHOOK cwp = s_cwp_hook;
    HHOOK getmsg = s_getmsg_hook;
    s_cwp_hook = NULL;
    s_getmsg_hook = NULL;
    if (cwp) UnhookWindowsHookEx(cwp);
    if (getmsg) UnhookWindowsHookEx(getmsg);
}

static bool restore_owned_detour() {
    BYTE* const target = s_target;
    if (!target || InterlockedCompareExchange(&s_patch_owned, 0, 0) == 0)
        return true;

    BYTE expected[kPatchLength] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
    const DWORD delta =
        (DWORD)(ULONG_PTR)&graphics_update_detour -
        (DWORD)(ULONG_PTR)(target + 5);
    memcpy(expected + 1, &delta, sizeof(delta));

    DWORD old_protect = 0;
    if (!VirtualProtect(target, kPatchLength, PAGE_EXECUTE_READWRITE,
                        &old_protect))
        return false;
    const LONG64 expected64 = load_long64(expected);
    const LONG64 original64 = load_long64(s_original);
    const bool restored =
        atomic_replace_8(target, expected64, original64) == expected64 &&
        FlushInstructionCache(GetCurrentProcess(), target, kPatchLength);
    DWORD ignored = 0;
    VirtualProtect(target, kPatchLength, old_protect, &ignored);
    if (restored) InterlockedExchange(&s_patch_owned, 0);
    return restored;
}

static RubyValue __cdecl graphics_update_detour(RubyValue self) {
    GraphicsUpdate_t original = s_trampoline;
    if (!original) return 4;

    if (GetCurrentThreadId() == s_owner_tid && s_ready)
        SetEvent(s_ready);

    // Le teardown est lui aussi effectue sur le thread proprietaire, a une
    // frontiere Graphics.update avant toute execution de callbacks. Le JMP
    // est retire avant de signaler le thread de controle; ce dernier attendra
    // ensuite que cet appel soit ressorti du trampoline avant de le liberer.
    if (GetCurrentThreadId() == s_owner_tid &&
        InterlockedCompareExchange(&s_state, DISPATCH_STOPPING,
                                   DISPATCH_STOPPING) == DISPATCH_STOPPING) {
        InterlockedIncrement(&s_detour_depth);
        InterlockedExchange(&s_stop_detached,
                            restore_owned_detour() ? 1 : 0);
        if (s_stop_complete) SetEvent(s_stop_complete);
        const RubyValue result = original(self);
        InterlockedDecrement(&s_detour_depth);
        return result;
    }

    // Un script evalue peut indirectement appeler Graphics.update. Seul le
    // niveau exterieur draine les callbacks; le niveau imbrique file droit
    // dans le trampoline original pour eviter toute reentrance du trainer.
    if (GetCurrentThreadId() != s_owner_tid)
        return original(self);

    if (InterlockedIncrement(&s_detour_depth) != 1) {
        const RubyValue result = original(self);
        InterlockedDecrement(&s_detour_depth);
        return result;
    }

    InterlockedIncrement(&s_frame_serial);

    CallbackEntry snapshot[kMaxCallbacks] = {};
    size_t count = 0;
    EnterCriticalSection(&s_lock);
    count = s_callback_count;
    if (count > kMaxCallbacks) count = kMaxCallbacks;
    if (count > 0)
        memcpy(snapshot, s_callbacks, count * sizeof(snapshot[0]));
    InterlockedIncrement(&s_snapshot_active);
    if (s_snapshot_idle) ResetEvent(s_snapshot_idle);
    LeaveCriticalSection(&s_lock);

    InterlockedExchange(&s_in_callbacks, 1);
    for (size_t index = 0; index < count; ++index) {
        // Un callback peut en desinscrire un autre pendant ce meme snapshot.
        // Revalider la paire juste avant l'appel empeche alors d'utiliser un
        // contexte que le proprietaire vient legitimement de liberer.
        bool still_registered = false;
        EnterCriticalSection(&s_lock);
        for (size_t live_index = 0; live_index < s_callback_count;
             ++live_index) {
            if (s_callbacks[live_index].callback == snapshot[index].callback &&
                s_callbacks[live_index].context == snapshot[index].context) {
                still_registered = true;
                break;
            }
        }
        LeaveCriticalSection(&s_lock);
        if (still_registered && snapshot[index].callback)
            snapshot[index].callback(snapshot[index].context);
    }
    InterlockedExchange(&s_in_callbacks, 0);

    if (InterlockedDecrement(&s_snapshot_active) == 0 && s_snapshot_idle)
        SetEvent(s_snapshot_idle);

    const RubyValue result = original(self);
    InterlockedDecrement(&s_detour_depth);
    return result;
}

} // namespace

// Diagnostic read-only utilise par les smoke tests externes. Aucun appel Ruby
// ni verrou : lire ce compteur ne peut pas reentrer dans RGSS.
extern "C" __declspec(dllexport) LONG __cdecl TrainerGetRgssFrameSerial() {
    return InterlockedCompareExchange(&s_frame_serial, 0, 0);
}

extern "C" __declspec(dllexport) LONG __cdecl TrainerGetRgssDispatchState() {
    return InterlockedCompareExchange(&s_state, 0, 0);
}

extern "C" __declspec(dllexport) BOOL __cdecl
TrainerGetRgssDispatchDiagnostics(RgssSafeDispatchDiagnostics* out) {
    if (!out) return FALSE;
    out->state = InterlockedCompareExchange(&s_state, 0, 0);
    out->stage = InterlockedCompareExchange(&s_diag_stage, 0, 0);
    out->error = (DWORD)InterlockedCompareExchange(&s_diag_error, 0, 0);
    out->eip = (DWORD)InterlockedCompareExchange(&s_diag_eip, 0, 0);
    out->attempts = InterlockedCompareExchange(&s_diag_attempts, 0, 0);
    out->game_tid = (DWORD)InterlockedCompareExchange(&s_diag_game_tid, 0, 0);
    return TRUE;
}

bool rgss_safe_dispatch_start(HMODULE owner, HWND game_hwnd, DWORD timeout_ms) {
    if (!owner || !game_hwnd) return false;
    if (InterlockedCompareExchange(&s_state, DISPATCH_STOPPED,
                                   DISPATCH_STOPPED) != DISPATCH_STOPPED)
        return InterlockedCompareExchange(&s_state, DISPATCH_RUNNING,
                                          DISPATCH_RUNNING) == DISPATCH_RUNNING;

    DWORD process_id = 0;
    const DWORD game_tid = GetWindowThreadProcessId(game_hwnd, &process_id);
    if (!game_tid || process_id != GetCurrentProcessId()) return false;

    if (InterlockedCompareExchange(&s_lock_ready, 1, 0) == 0) {
        InitializeCriticalSection(&s_lock);
        InterlockedExchange(&s_lock_ready, 2);
    }
    while (InterlockedCompareExchange(&s_lock_ready, 0, 0) == 1)
        Sleep(0);
    s_snapshot_idle = CreateEventA(NULL, TRUE, TRUE, NULL);
    s_ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    s_stop_complete = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!s_snapshot_idle || !s_ready || !s_stop_complete) {
        rgss_safe_dispatch_shutdown();
        return false;
    }

    s_owner = owner;
    s_game = game_hwnd;
    s_game_tid = game_tid;
    InterlockedExchange(&s_diag_error, ERROR_SUCCESS);
    InterlockedExchange(&s_diag_eip, 0);
    InterlockedExchange(&s_diag_attempts, 0);
    InterlockedExchange(&s_diag_game_tid, (LONG)game_tid);
    set_diagnostic_stage(RGSS_DISPATCH_STAGE_STARTING);
    InterlockedExchange(&s_frame_serial, 0);
    InterlockedExchange(&s_state, DISPATCH_BOOTSTRAP);
    s_cwp_hook = SetWindowsHookExA(WH_CALLWNDPROC, cwp_hook, owner, game_tid);
    s_getmsg_hook = SetWindowsHookExA(WH_GETMESSAGE, getmsg_hook, owner,
                                      game_tid);
    // Les hooks sont une voie rapide, pas une dependance : Uranium peut ne
    // plus pomper de messages lorsqu'il est inactif et certains environnements
    // refusent un des deux hooks. Le fallback natif borne reste disponible.
    set_diagnostic_stage(RGSS_DISPATCH_STAGE_HOOKS_ARMED);

    rgss_safe_dispatch_notify();
    const DWORD started = GetTickCount();
    DWORD wait = WAIT_TIMEOUT;
    do {
        wait = WaitForSingleObject(s_ready, 100);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
        if (elapsed_since(started) >= kHookGraceMs)
            install_with_suspended_game_thread(
                remaining_timeout(started, timeout_ms));
    } while (GetTickCount() - started < timeout_ms);
    remove_bootstrap_hooks();
    if (wait != WAIT_OBJECT_0 ||
        InterlockedCompareExchange(&s_state, DISPATCH_RUNNING,
                                   DISPATCH_RUNNING) != DISPATCH_RUNNING) {
        rgss_safe_dispatch_shutdown();
        return false;
    }
    return true;
}

bool rgss_safe_dispatch_shutdown() {
    const LONG previous = InterlockedExchange(&s_state, DISPATCH_STOPPING);
    remove_bootstrap_hooks();

    // Ne jamais modifier le prologue depuis le thread de controle pendant que
    // Graphics.update peut s'y engager. Le detour se retire lui-meme au frame
    // suivant. Si la VM ne produit plus de frames, conserver le trampoline :
    // un payload injecte n'est de toute facon pas decharge par le launcher.
    bool detached = previous != DISPATCH_RUNNING || !s_target;
    if (previous == DISPATCH_RUNNING && s_target) {
        if (s_stop_complete) ResetEvent(s_stop_complete);
        InterlockedExchange(&s_stop_detached, 0);
        rgss_safe_dispatch_notify();
        detached = s_stop_complete &&
                   WaitForSingleObject(s_stop_complete, 3000) == WAIT_OBJECT_0 &&
                   InterlockedCompareExchange(&s_stop_detached, 0, 0) == 1;
    }

    // Meme si aucun nouveau frame n'arrive pour retirer le JMP, un snapshot
    // deja commence doit terminer avant que shutdown rende la main. Les frames
    // suivants voient STOPPING et ne construisent plus aucun snapshot.
    if (s_snapshot_idle)
        WaitForSingleObject(s_snapshot_idle, INFINITE);

    if (detached) {
        while (InterlockedCompareExchange(&s_detour_depth, 0, 0) != 0 ||
               InterlockedCompareExchange(&s_snapshot_active, 0, 0) != 0)
            Sleep(0);
    }

    GraphicsUpdate_t trampoline = s_trampoline;
    if (detached) {
        s_trampoline = NULL;
        s_target = NULL;
        s_eval = NULL;
        s_owner_tid = 0;
        if (trampoline) VirtualFree((void*)trampoline, 0, MEM_RELEASE);
    }

    if (InterlockedCompareExchange(&s_lock_ready, 0, 0) == 2) {
        EnterCriticalSection(&s_lock);
        s_callback_count = 0;
        ZeroMemory(s_callbacks, sizeof(s_callbacks));
        LeaveCriticalSection(&s_lock);
    }
    if (s_ready) { CloseHandle(s_ready); s_ready = NULL; }
    if (s_stop_complete) {
        CloseHandle(s_stop_complete);
        s_stop_complete = NULL;
    }
    if (s_snapshot_idle) {
        CloseHandle(s_snapshot_idle);
        s_snapshot_idle = NULL;
    }
    s_owner = NULL;
    s_game = NULL;
    s_game_tid = 0;
    InterlockedExchange(&s_in_callbacks, 0);
    if (detached) {
        InterlockedExchange(&s_detour_depth, 0);
        InterlockedExchange(&s_state, DISPATCH_STOPPED);
    }
    return detached;
}

bool rgss_safe_dispatch_is_stopping() {
    const LONG state = InterlockedCompareExchange(&s_state, 0, 0);
    return state == DISPATCH_STOPPING || state == DISPATCH_STOPPED ||
           state == DISPATCH_FAILED;
}

bool rgss_safe_dispatch_register(RgssSafeCallback callback, void* context) {
    if (!callback || InterlockedCompareExchange(&s_lock_ready, 0, 0) != 2 ||
        InterlockedCompareExchange(&s_state, DISPATCH_RUNNING,
                                   DISPATCH_RUNNING) != DISPATCH_RUNNING)
        return false;

    bool inserted = false;
    EnterCriticalSection(&s_lock);
    bool duplicate = false;
    for (size_t index = 0; index < s_callback_count; ++index) {
        if (s_callbacks[index].callback == callback &&
            s_callbacks[index].context == context) {
            duplicate = true;
            break;
        }
    }
    if (!duplicate && s_callback_count < kMaxCallbacks) {
        s_callbacks[s_callback_count].callback = callback;
        s_callbacks[s_callback_count].context = context;
        ++s_callback_count;
        inserted = true;
    }
    LeaveCriticalSection(&s_lock);
    return inserted;
}

void rgss_safe_dispatch_unregister(RgssSafeCallback callback, void* context) {
    if (!callback || InterlockedCompareExchange(&s_lock_ready, 0, 0) != 2)
        return;

    EnterCriticalSection(&s_lock);
    for (size_t index = 0; index < s_callback_count; ++index) {
        if (s_callbacks[index].callback == callback &&
            s_callbacks[index].context == context) {
            --s_callback_count;
            if (index < s_callback_count)
                memmove(&s_callbacks[index], &s_callbacks[index + 1],
                        (s_callback_count - index) * sizeof(s_callbacks[0]));
            ZeroMemory(&s_callbacks[s_callback_count],
                       sizeof(s_callbacks[s_callback_count]));
            break;
        }
    }
    LeaveCriticalSection(&s_lock);

    // Ne pas attendre notre propre snapshot si un callback se desinscrit.
    if (!rgss_safe_dispatch_is_safe_thread() && s_snapshot_idle)
        WaitForSingleObject(s_snapshot_idle, INFINITE);
}

bool rgss_safe_dispatch_flush(DWORD timeout_ms) {
    if (rgss_safe_dispatch_is_safe_thread()) return false;
    HANDLE complete = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!complete) return false;

    CallbackBarrier barrier = {complete};
    if (!rgss_safe_dispatch_register(signal_callback_barrier, &barrier)) {
        CloseHandle(complete);
        return false;
    }

    rgss_safe_dispatch_notify();
    const bool completed =
        WaitForSingleObject(complete, timeout_ms) == WAIT_OBJECT_0;
    rgss_safe_dispatch_unregister(signal_callback_barrier, &barrier);
    CloseHandle(complete);
    return completed;
}

void rgss_safe_dispatch_notify() {
    if (s_game) PostMessageA(s_game, WM_NULL, 0, 0);
    if (s_game_tid) PostThreadMessageA(s_game_tid, WM_NULL, 0, 0);
}

bool rgss_safe_dispatch_is_safe_thread() {
    return GetCurrentThreadId() == s_owner_tid &&
           InterlockedCompareExchange(&s_in_callbacks, 0, 0) != 0;
}

int rgss_safe_eval(const char* script) {
    RGSSEval_t eval = s_eval;
    if (!script || !eval || !rgss_safe_dispatch_is_safe_thread()) return -1;
    return eval(script);
}
