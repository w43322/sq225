/**
 * Engine API — native Squirrel functions that operate on Squirrel objects.
 *
 * These are registered directly as SQFUNCTIONs in the root table,
 * bypassing the JS callback path (which only supports basic types).
 */

#include <squirrel.h>
#include <cstdio>

// ── Helper: register a native closure in the root table ────

static void register_func(HSQUIRRELVM vm, const char *name, SQFUNCTION func) {
    sq_pushroottable(vm);
    sq_pushstring(vm, name, -1);
    sq_newclosure(vm, func, 0);
    sq_newslot(vm, -3, SQFalse);
    sq_poptop(vm); // pop root table
}

// ── createFromScript(componentId, classRef = null) ─────────
// If classRef is a class, instantiate it. Otherwise return empty table.

static SQInteger sq_createFromScript(HSQUIRRELVM vm) {
    SQInteger top = sq_gettop(vm);
    printf("[engine_api] createFromScript called, top=%d\n", top);

    if (top >= 3 && sq_gettype(vm, 3) == OT_CLASS) {
        printf("[engine_api] createFromScript: instantiating class\n");
        // Call the class as a constructor: classRef()
        // This creates the instance AND calls the constructor.
        sq_push(vm, 3);             // push class (acts as closure)
        sq_pushroottable(vm);       // push 'this' for the call
        if (SQ_FAILED(sq_call(vm, 1, SQTrue, SQTrue))) {
            printf("[engine_api] createFromScript: sq_call FAILED\n");
            sq_poptop(vm);
            sq_pushnull(vm);
            return 1;
        }
        printf("[engine_api] createFromScript: success, type=%d\n", sq_gettype(vm, -1));
        sq_remove(vm, -2);
        return 1;
    }

    printf("[engine_api] createFromScript: no class, returning table\n");
    sq_newtable(vm);
    return 1;
}

// ── shutdown(obj) ──────────────────────────────────────────
// No-op for now. In the real engine this destroys the C++ component.

static SQInteger sq_shutdown(HSQUIRRELVM vm) {
    (void)vm;
    return 0;
}

// ── setIdentificator(obj, name) ────────────────────────────
// Store "__id" slot on the object.

static SQInteger sq_setIdentificator(HSQUIRRELVM vm) {
    // stack: [this(1), obj(2), name(3)]
    sq_push(vm, 2);                 // push obj
    sq_pushstring(vm, "__id", -1);  // push key
    sq_push(vm, 3);                 // push value (name)
    sq_rawset(vm, -3);              // obj["__id"] = name
    sq_poptop(vm);                  // pop obj
    return 0;
}

// ── getIdentificator(obj) ──────────────────────────────────
// Read "__id" from the object.

static SQInteger sq_getIdentificator(HSQUIRRELVM vm) {
    // stack: [this(1), obj(2)]
    sq_push(vm, 2);                 // push obj
    sq_pushstring(vm, "__id", -1);  // push key
    if (SQ_SUCCEEDED(sq_rawget(vm, -2))) {
        // stack: [obj, value]
        sq_remove(vm, -2);          // remove obj, keep value
        return 1;
    }
    sq_poptop(vm);                  // pop obj
    sq_pushstring(vm, "unknown", -1);
    return 1;
}

// ── RegisterObject(name, obj) ──────────────────────────────
// Store object in ::__registry[name].

static SQInteger sq_RegisterObject(HSQUIRRELVM vm) {
    // stack: [this(1), name(2), obj(3)]
    // Get or create ::__registry
    sq_pushroottable(vm);
    sq_pushstring(vm, "__registry", -1);
    if (SQ_FAILED(sq_rawget(vm, -2))) {
        // Create __registry table
        sq_pushstring(vm, "__registry", -1);
        sq_newtable(vm);
        sq_rawset(vm, -3);          // root["__registry"] = {}
        // Re-fetch it
        sq_pushstring(vm, "__registry", -1);
        sq_rawget(vm, -2);
    }
    // stack: [..., roottable, __registry]

    sq_push(vm, 2);                 // push name (key)
    sq_push(vm, 3);                 // push obj (value)
    sq_newslot(vm, -3, SQFalse);    // __registry[name] <- obj
    sq_pop(vm, 2);                  // pop __registry + roottable
    return 0;
}

// ── Registration ───────────────────────────────────────────

void register_engine_api(HSQUIRRELVM vm) {
    register_func(vm, "createFromScript",  sq_createFromScript);
    register_func(vm, "shutdown",          sq_shutdown);
    register_func(vm, "setIdentificator",  sq_setIdentificator);
    register_func(vm, "getIdentificator",  sq_getIdentificator);
    register_func(vm, "RegisterObject",    sq_RegisterObject);
}
