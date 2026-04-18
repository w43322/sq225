/**
 * Engine API — native Squirrel functions that operate on Squirrel objects.
 *
 * These are registered directly as SQFUNCTIONs, bypassing the JS callback
 * path (which can't modify Squirrel tables/instances in place).
 *
 * Only functions that MUST manipulate Squirrel objects on the stack live here.
 * Everything else belongs in TypeScript via vm.set().
 */

#include <squirrel.h>
#include <cstring>

// ── Helper: register a native closure in the root table ────

static void register_func(HSQUIRRELVM vm, const char *name, SQFUNCTION func) {
    sq_pushroottable(vm);
    sq_pushstring(vm, name, -1);
    sq_newclosure(vm, func, 0);
    sq_newslot(vm, -3, SQFalse);
    sq_poptop(vm);
}

// Helper: push a native closure as a slot on the table at stack position tblIdx
static void add_method(HSQUIRRELVM vm, SQInteger tblIdx, const char *name, SQFUNCTION func) {
    sq_pushstring(vm, name, -1);
    sq_newclosure(vm, func, 0);
    sq_newslot(vm, tblIdx < 0 ? tblIdx - 2 : tblIdx, SQFalse);
}

// ── Component methods: RegisterObject / UnregisterObject / GetObject ──
// These operate on "this._objects" — a per-component child registry.
// In the original engine, each component maintains its own named sub-objects.

static SQInteger sq_comp_RegisterObject(HSQUIRRELVM vm) {
    // stack: [this(1), name(2), obj(3)]
    sq_push(vm, 1);                     // push this
    sq_pushstring(vm, "_objects", -1);
    if (SQ_FAILED(sq_rawget(vm, -2))) {
        // _objects doesn't exist yet, create it
        sq_pushstring(vm, "_objects", -1);
        sq_newtable(vm);
        sq_rawset(vm, -3);              // this._objects = {}
        sq_pushstring(vm, "_objects", -1);
        sq_rawget(vm, -2);
    }
    // stack: [..., this, _objects]
    sq_push(vm, 2);                     // push name
    sq_push(vm, 3);                     // push obj
    sq_newslot(vm, -3, SQFalse);        // _objects[name] <- obj
    sq_pop(vm, 2);                      // pop _objects + this
    return 0;
}

static SQInteger sq_comp_UnregisterObject(HSQUIRRELVM vm) {
    // stack: [this(1), name(2)]
    sq_push(vm, 1);
    sq_pushstring(vm, "_objects", -1);
    if (SQ_SUCCEEDED(sq_rawget(vm, -2))) {
        // stack: [..., this, _objects]
        sq_push(vm, 2);                 // push name
        sq_deleteslot(vm, -2, SQFalse); // delete _objects[name]
        sq_pop(vm, 2);                  // pop _objects + this
    } else {
        sq_poptop(vm);                  // pop this
    }
    return 0;
}

static SQInteger sq_comp_GetObject(HSQUIRRELVM vm) {
    // stack: [this(1), name(2)]
    sq_push(vm, 1);
    sq_pushstring(vm, "_objects", -1);
    if (SQ_SUCCEEDED(sq_rawget(vm, -2))) {
        // stack: [..., this, _objects]
        sq_push(vm, 2);                 // push name
        if (SQ_SUCCEEDED(sq_get(vm, -2))) {
            // stack: [..., this, _objects, value]
            sq_remove(vm, -2);           // remove _objects
            sq_remove(vm, -2);           // remove this
            return 1;
        }
        sq_poptop(vm);                   // pop _objects
    }
    sq_poptop(vm);                       // pop this
    sq_pushnull(vm);
    return 1;
}

// ── getComponent(name) ────────────────────────────────────
// Returns a fresh clone of the matching component template table,
// with _objects table and RegisterObject/UnregisterObject/GetObject
// methods attached.

static SQInteger sq_getComponent(HSQUIRRELVM vm) {
    const SQChar *name = nullptr;
    if (sq_gettop(vm) >= 2 && sq_gettype(vm, 2) == OT_STRING) {
        sq_getstring(vm, 2, &name);
    }

    const char *tableName = "Generic";
    if (name) {
        if (strcmp(name, "ResourceManager") == 0) tableName = "ResourceManager";
        else if (strcmp(name, "SoundDriver") == 0) tableName = "SoundDriver";
        else if (strcmp(name, "Display") == 0) tableName = "Display";
    }

    sq_pushroottable(vm);
    sq_pushstring(vm, tableName, -1);
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_poptop(vm);
        sq_newtable(vm);
        return 1;
    }
    if (SQ_FAILED(sq_clone(vm, -1))) {
        sq_remove(vm, -2);
        return 1;
    }
    sq_remove(vm, -2);       // remove original
    sq_remove(vm, -2);       // remove root table
    // stack: [clone]

    // Attach _objects table and methods to the clone
    SQInteger cloneIdx = sq_gettop(vm);

    sq_pushstring(vm, "_objects", -1);
    sq_newtable(vm);
    sq_newslot(vm, cloneIdx, SQFalse);

    add_method(vm, cloneIdx, "RegisterObject", sq_comp_RegisterObject);
    add_method(vm, cloneIdx, "UnregisterObject", sq_comp_UnregisterObject);
    add_method(vm, cloneIdx, "GetObject", sq_comp_GetObject);

    return 1;
}

// ── loadFromScript(component, scriptClass) ─────────────────
// Copy all fields and methods from scriptClass onto the component table.

static SQInteger sq_loadFromScript(HSQUIRRELVM vm) {
    SQInteger top = sq_gettop(vm);
    if (top < 3) {
        sq_pushbool(vm, SQTrue);
        return 1;
    }

    SQObjectType classType = sq_gettype(vm, 3);
    if (classType != OT_CLASS && classType != OT_TABLE) {
        sq_pushbool(vm, SQTrue);
        return 1;
    }

    sq_push(vm, 3);           // push class to iterate
    sq_pushnull(vm);           // push null iterator
    while (SQ_SUCCEEDED(sq_next(vm, -2))) {
        sq_push(vm, 2);       // push component
        sq_push(vm, -3);      // push key
        sq_push(vm, -3);      // push value
        sq_newslot(vm, -3, SQFalse);
        sq_poptop(vm);        // pop component
        sq_pop(vm, 2);        // pop key and value
    }
    sq_pop(vm, 2);            // pop iterator and class

    sq_pushbool(vm, SQTrue);
    return 1;
}

// ── setIdentificator(obj, name) ────────────────────────────

static SQInteger sq_setIdentificator(HSQUIRRELVM vm) {
    sq_push(vm, 2);
    sq_pushstring(vm, "__id", -1);
    sq_push(vm, 3);
    sq_rawset(vm, -3);
    sq_poptop(vm);
    return 0;
}

// ── getIdentificator(obj) ──────────────────────────────────

static SQInteger sq_getIdentificator(HSQUIRRELVM vm) {
    sq_push(vm, 2);
    sq_pushstring(vm, "__id", -1);
    if (SQ_SUCCEEDED(sq_rawget(vm, -2))) {
        sq_remove(vm, -2);
        return 1;
    }
    sq_poptop(vm);
    sq_pushstring(vm, "unknown", -1);
    return 1;
}

// ── Registration ───────────────────────────────────────────

void register_engine_api(HSQUIRRELVM vm) {
    register_func(vm, "getComponent",     sq_getComponent);
    register_func(vm, "loadFromScript",   sq_loadFromScript);
    register_func(vm, "setIdentificator", sq_setIdentificator);
    register_func(vm, "getIdentificator", sq_getIdentificator);
}
