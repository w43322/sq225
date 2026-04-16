/**
 * C-side printf wrapper for Squirrel's SQPRINTFUNCTION.
 *
 * Squirrel's print function signature is variadic:
 *   void (*SQPRINTFUNCTION)(HSQUIRRELVM v, const SQChar *fmt, ...)
 *
 * Emscripten's addFunction() can't create variadic WASM callbacks,
 * so we provide C wrappers that call vsnprintf then invoke fixed-
 * signature JS callbacks with the formatted string.
 */

#include <cstdarg>
#include <cstdio>
#include "squirrel.h"
#include "sqstdaux.h"

extern "C" {

/* JS callback: void(const char* formatted_str) — set from JS side */
typedef void (*JsPrintCallback)(const char *str);

static JsPrintCallback g_jsPrint = 0;
static JsPrintCallback g_jsErrorPrint = 0;

static char g_printBuf[4096];

/* Matches SQPRINTFUNCTION: void(HSQUIRRELVM, const SQChar* fmt, ...) */
static void bridge_printfunc(HSQUIRRELVM vm, const SQChar *fmt, ...) {
    (void)vm;
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_printBuf, sizeof(g_printBuf), fmt, args);
    va_end(args);
    if (g_jsPrint) g_jsPrint(g_printBuf);
}

static void bridge_errorfunc(HSQUIRRELVM vm, const SQChar *fmt, ...) {
    (void)vm;
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_printBuf, sizeof(g_printBuf), fmt, args);
    va_end(args);
    if (g_jsErrorPrint) g_jsErrorPrint(g_printBuf);
}

/**
 * One-call setup: registers print functions + stdlib error handlers.
 * Call from JS after sq_open() and sqstd_register_*lib().
 *
 * @param vm          The Squirrel VM handle
 * @param printCb     JS callback for normal print output
 * @param errorCb     JS callback for error output
 */
void sq_bridge_setup(HSQUIRRELVM vm, JsPrintCallback printCb, JsPrintCallback errorCb) {
    g_jsPrint = printCb;
    g_jsErrorPrint = errorCb;

    /* Set the variadic C print functions — these handle printf formatting
       in C, then call the fixed-signature JS callbacks */
    sq_setprintfunc(vm, bridge_printfunc);

    /* sqstd_seterrorhandlers internally uses sq_getprintfunc() to get
       bridge_printfunc, then calls it with printf-style args — works
       because it's a real variadic C function now */
    sqstd_seterrorhandlers(vm);
}

} /* extern "C" */
