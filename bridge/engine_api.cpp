/**
 * Engine API — native Squirrel functions that operate on Squirrel objects.
 *
 * These are registered directly as SQFUNCTIONs in the root table,
 * bypassing the JS callback path (which only supports basic types).
 */

#include <squirrel.h>
#include <cstdio>
#include <cstring>

// ── Helper: register a native closure in the root table ────

static void register_func(HSQUIRRELVM vm, const char *name, SQFUNCTION func) {
    sq_pushroottable(vm);
    sq_pushstring(vm, name, -1);
    sq_newclosure(vm, func, 0);
    sq_newslot(vm, -3, SQFalse);
    sq_poptop(vm); // pop root table
}

// ── getComponent(name) ────────────────────────────────────
// Returns a fresh clone of the matching component template table.
// Looks up globals by component name; defaults to ::Generic.

static SQInteger sq_getComponent(HSQUIRRELVM vm) {
    const SQChar *name = nullptr;
    if (sq_gettop(vm) >= 2 && sq_gettype(vm, 2) == OT_STRING) {
        sq_getstring(vm, 2, &name);
    }

    // Map component name to global table name.
    // Specialized components have their own table; everything else uses Generic.
    const char *tableName = "Generic";
    if (name) {
        if (strcmp(name, "ResourceManager") == 0) tableName = "ResourceManager";
        else if (strcmp(name, "SoundDriver") == 0) tableName = "SoundDriver";
        else if (strcmp(name, "Display") == 0) tableName = "Display";
    }

    // Fetch from root table
    sq_pushroottable(vm);
    sq_pushstring(vm, tableName, -1);
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_poptop(vm);       // pop root table
        sq_newtable(vm);
        return 1;
    }
    // Clone so each caller gets a fresh mutable copy
    if (SQ_FAILED(sq_clone(vm, -1))) {
        sq_remove(vm, -2);
        return 1;
    }
    sq_remove(vm, -2);       // remove original
    sq_remove(vm, -2);       // remove root table
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

// ── loadFromScript(component, scriptClass) ─────────────────
// Copy all fields and methods from scriptClass onto the component table.
// This is the core of the engine's script-to-component binding:
// getComponent() creates a native component (table), loadFromScript()
// populates it with the Squirrel class's members so it can act as that class.

static SQInteger sq_loadFromScript(HSQUIRRELVM vm) {
    SQInteger top = sq_gettop(vm);
    // stack: [this(1), component(2), scriptClass(3)]
    if (top < 3) {
        sq_pushbool(vm, SQTrue);
        return 1;
    }

    SQObjectType classType = sq_gettype(vm, 3);
    if (classType != OT_CLASS && classType != OT_TABLE) {
        // Not a class or table, nothing to copy
        sq_pushbool(vm, SQTrue);
        return 1;
    }

    // Iterate the class/table and copy all members onto the component
    sq_push(vm, 3);           // push class to iterate
    sq_pushnull(vm);           // push null iterator
    while (SQ_SUCCEEDED(sq_next(vm, -2))) {
        // stack: [..., class, iterator, key, value]
        sq_push(vm, 2);       // push component
        sq_push(vm, -3);      // push key (copy)
        sq_push(vm, -3);      // push value (copy)
        sq_newslot(vm, -3, SQFalse);  // component[key] <- value
        sq_poptop(vm);        // pop component
        sq_pop(vm, 2);        // pop key and value
    }
    sq_pop(vm, 2);            // pop iterator and class

    sq_pushbool(vm, SQTrue);
    return 1;
}

// ── Registration ───────────────────────────────────────────

void register_engine_api(HSQUIRRELVM vm) {
    register_func(vm, "getComponent",      sq_getComponent);
    register_func(vm, "loadFromScript",    sq_loadFromScript);
    register_func(vm, "shutdown",          sq_shutdown);
    register_func(vm, "setIdentificator",  sq_setIdentificator);
    register_func(vm, "getIdentificator",  sq_getIdentificator);
    register_func(vm, "RegisterObject",    sq_RegisterObject);
}
