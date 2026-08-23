#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Callback execute sur le thread RGSS, juste avant le vrai Graphics.update.
// Il doit rester court et ne doit pas appeler Graphics.update recursivement.
typedef void (__cdecl *RgssSafeCallback)(void* context);

// Etapes du bootstrap exposees aux smoke tests externes. "error" dans le
// snapshot ci-dessous contient un code Win32 (GetLastError ou ERROR_*).
enum RgssSafeDispatchStage {
    RGSS_DISPATCH_STAGE_IDLE = 0,
    RGSS_DISPATCH_STAGE_STARTING = 1,
    RGSS_DISPATCH_STAGE_HOOKS_ARMED = 2,
    RGSS_DISPATCH_STAGE_HOOK_CLAIMED = 3,
    RGSS_DISPATCH_STAGE_WORKER_CLAIMED = 4,
    RGSS_DISPATCH_STAGE_VALIDATING = 5,
    RGSS_DISPATCH_STAGE_TRAMPOLINE_READY = 6,
    RGSS_DISPATCH_STAGE_TARGET_WRITABLE = 7,
    RGSS_DISPATCH_STAGE_SUSPENDING = 8,
    RGSS_DISPATCH_STAGE_EIP_BUSY = 9,
    RGSS_DISPATCH_STAGE_COMMITTING = 10,
    RGSS_DISPATCH_STAGE_RESUMING = 11,
    RGSS_DISPATCH_STAGE_RUNNING = 12,
    RGSS_DISPATCH_STAGE_FAILED = 13,
    RGSS_DISPATCH_STAGE_SHUTDOWN = 14
};

struct RgssSafeDispatchDiagnostics {
    LONG state;
    LONG stage;
    DWORD error;
    DWORD eip;
    LONG attempts;
    DWORD game_tid;
};

extern "C" __declspec(dllexport) BOOL __cdecl
TrainerGetRgssDispatchDiagnostics(RgssSafeDispatchDiagnostics* out);

// Installe le point de passage unique du trainer. L'installation native est
// demandee au thread proprietaire de la fenetre sans evaluer de Ruby depuis
// le hook Windows. La fonction attend au plus timeout_ms que le premier frame
// RGSS confirme le detour.
bool rgss_safe_dispatch_start(HMODULE owner, HWND game_hwnd, DWORD timeout_ms);

// Retire le detour et libere ses ressources. A appeler avant de decharger le
// payload; les callbacks ne sont jamais executes apres le retour.
// Returns true only when the Graphics.update detour was safely removed and
// the payload can be unloaded without leaving a callback in RGSS.
bool rgss_safe_dispatch_shutdown();

// Retry workers use this to exit before the payload is unloaded.
bool rgss_safe_dispatch_is_stopping();

// Registre un callback permanent, invoque une fois au debut de chaque frame.
// Les doublons exacts (callback, contexte) sont rejetes. unregister attend la
// fin d'un snapshot eventuellement en cours avant de rendre le contexte libre.
bool rgss_safe_dispatch_register(RgssSafeCallback callback, void* context);
void rgss_safe_dispatch_unregister(RgssSafeCallback callback, void* context);

// Hint sans garantie de latence : reveille la pompe Win32 pendant le bootstrap.
// Une fois le detour confirme, les callbacks sont naturellement draines a
// chaque frame et n'ont pas besoin de notifier.
void rgss_safe_dispatch_notify();

// Vrai uniquement pendant la phase de callbacks au debut de Graphics.update.
bool rgss_safe_dispatch_is_safe_thread();

// Evaluation synchrone reservee aux callbacks du dispatcher. Elle renvoie -1
// hors du safe point ou lorsque RGSS n'est pas la build strictement supportee.
int rgss_safe_eval(const char* script);
