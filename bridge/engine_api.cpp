/**
 * Engine API — native Squirrel functions for the component system.
 *
 * Components are userdata objects holding an entityId (uint32_t).
 * Each userdata has a delegate table that stores script-defined fields/methods
 * (copied by loadFromScript) and metamethods (_set, _newslot, _nexti).
 *
 * The delegate table approach mirrors the original C++ engine:
 * - userdata = C++ component identity (entityId)
 * - delegate = Squirrel script properties + methods
 * - metamethods bridge between Squirrel slot operations and the delegate
 */

#include <squirrel.h>
#include <cstdio>
#include <cstring>

static uint32_t s_nextEntityId = 1;

// ── Helper: register a native closure in a table at stack index ────

static void register_func(HSQUIRRELVM vm, const char *name, SQFUNCTION func) {
    sq_pushroottable(vm);
    sq_pushstring(vm, name, -1);
    sq_newclosure(vm, func, 0);
    sq_newslot(vm, -3, SQFalse);
    sq_poptop(vm);
}

static void add_method(HSQUIRRELVM vm, SQInteger tblIdx, const char *name, SQFUNCTION func) {
    sq_pushstring(vm, name, -1);
    sq_newclosure(vm, func, 0);
    sq_newslot(vm, tblIdx < 0 ? tblIdx - 2 : tblIdx, SQFalse);
}

// ── Helper: get entityId from userdata at stack position ────

static uint32_t getEntityId(HSQUIRRELVM vm, SQInteger idx) {
    SQUserPointer p;
    if (SQ_SUCCEEDED(sq_getuserdata(vm, idx, &p, nullptr))) {
        return *static_cast<uint32_t*>(p);
    }
    return 0;
}

// ── Helper: get delegate table from userdata at stack position ────
// Pushes the delegate table on success, returns true.

static bool pushDelegate(HSQUIRRELVM vm, SQInteger idx) {
    if (sq_gettype(vm, idx) != OT_USERDATA) return false;
    sq_push(vm, idx);
    if (SQ_FAILED(sq_getdelegate(vm, -1))) {
        sq_poptop(vm);
        return false;
    }
    sq_remove(vm, -2); // remove userdata copy, keep delegate
    return true;
}

// ── Metamethods for component userdata ────────────────────

// _set(key, val): store in delegate table
static SQInteger meta_set(HSQUIRRELVM vm) {
    // stack: [userdata(1), key(2), val(3)]
    if (!pushDelegate(vm, 1)) return SQ_ERROR;
    // stack: [..., delegate]
    sq_push(vm, 2);  // key
    sq_push(vm, 3);  // val
    sq_rawset(vm, -3);
    sq_poptop(vm);   // pop delegate
    return 0;
}

// _newslot(key, val): create new slot in delegate table
static SQInteger meta_newslot(HSQUIRRELVM vm) {
    // stack: [userdata(1), key(2), val(3)]
    if (!pushDelegate(vm, 1)) return SQ_ERROR;
    sq_push(vm, 2);  // key
    sq_push(vm, 3);  // val
    sq_newslot(vm, -3, SQFalse);
    sq_poptop(vm);   // pop delegate
    return 0;
}

// _nexti(prev): iterate delegate table entries
static SQInteger meta_nexti(HSQUIRRELVM vm) {
    // stack: [userdata(1), prev(2)]
    if (!pushDelegate(vm, 1)) {
        sq_pushnull(vm);
        return 1;
    }
    // stack: [..., delegate]
    sq_push(vm, 2);  // push prev iterator
    if (SQ_SUCCEEDED(sq_next(vm, -2))) {
        // stack: [..., delegate, iter, key, val]
        // Return the key as the new iterator position
        sq_remove(vm, -1);  // remove val
        sq_remove(vm, -2);  // remove old iter
        sq_remove(vm, -2);  // remove delegate
        return 1;           // return key
    }
    sq_poptop(vm);   // pop delegate
    sq_pushnull(vm); // no more items
    return 1;
}

// _tostring(): return "(component: <entityId>)"
static SQInteger meta_tostring(HSQUIRRELVM vm) {
    uint32_t id = getEntityId(vm, 1);
    char buf[64];
    snprintf(buf, sizeof(buf), "(component: %u)", id);
    sq_pushstring(vm, buf, -1);
    return 1;
}

// ── Component child registry: RegisterObject / UnregisterObject / GetObject ──
// Operate on delegate._objects sub-table.

static bool pushObjects(HSQUIRRELVM vm, SQInteger selfIdx, bool create) {
    if (!pushDelegate(vm, selfIdx)) return false;
    // stack: [..., delegate]
    sq_pushstring(vm, "_objects", -1);
    if (SQ_SUCCEEDED(sq_rawget(vm, -2))) {
        // stack: [..., delegate, _objects]
        sq_remove(vm, -2); // remove delegate, keep _objects
        return true;
    }
    if (create) {
        // Create _objects on delegate
        sq_pushstring(vm, "_objects", -1);
        sq_newtable(vm);
        sq_rawset(vm, -3); // delegate._objects = {}
        // Re-fetch
        sq_pushstring(vm, "_objects", -1);
        sq_rawget(vm, -2);
        sq_remove(vm, -2);
        return true;
    }
    sq_poptop(vm); // pop delegate
    return false;
}

static SQInteger sq_comp_RegisterObject(HSQUIRRELVM vm) {
    if (!pushObjects(vm, 1, true)) return 0;
    sq_push(vm, 2); // name
    sq_push(vm, 3); // obj
    sq_newslot(vm, -3, SQFalse);
    sq_poptop(vm);
    return 0;
}

static SQInteger sq_comp_UnregisterObject(HSQUIRRELVM vm) {
    if (!pushObjects(vm, 1, false)) return 0;
    sq_push(vm, 2);
    sq_deleteslot(vm, -2, SQFalse);
    sq_poptop(vm);
    return 0;
}

static SQInteger sq_comp_GetObject(HSQUIRRELVM vm) {
    if (!pushObjects(vm, 1, false)) {
        sq_pushnull(vm);
        return 1;
    }
    sq_push(vm, 2);
    if (SQ_SUCCEEDED(sq_get(vm, -2))) {
        sq_remove(vm, -2);
        return 1;
    }
    sq_poptop(vm);
    sq_pushnull(vm);
    return 1;
}

// ── createComponent: build userdata + delegate ────────────

static void createComponent(HSQUIRRELVM vm, const char *templateName) {
    uint32_t entityId = s_nextEntityId++;

    // Create userdata with entityId
    uint32_t *p = static_cast<uint32_t*>(sq_newuserdata(vm, sizeof(uint32_t)));
    *p = entityId;
    // stack: [userdata]

    // Create delegate table
    sq_newtable(vm);
    SQInteger delegateIdx = sq_gettop(vm);

    // Store entityId and type as readable slots
    sq_pushstring(vm, "__entityId", -1);
    sq_pushinteger(vm, entityId);
    sq_newslot(vm, delegateIdx, SQFalse);

    sq_pushstring(vm, "__type", -1);
    sq_pushstring(vm, templateName, -1);
    sq_newslot(vm, delegateIdx, SQFalse);

    // Add metamethods
    add_method(vm, delegateIdx, "_set", meta_set);
    add_method(vm, delegateIdx, "_newslot", meta_newslot);
    add_method(vm, delegateIdx, "_nexti", meta_nexti);
    add_method(vm, delegateIdx, "_tostring", meta_tostring);

    // Add component methods
    add_method(vm, delegateIdx, "RegisterObject", sq_comp_RegisterObject);
    add_method(vm, delegateIdx, "UnregisterObject", sq_comp_UnregisterObject);
    add_method(vm, delegateIdx, "GetObject", sq_comp_GetObject);

    // Create _objects sub-table
    sq_pushstring(vm, "_objects", -1);
    sq_newtable(vm);
    sq_newslot(vm, delegateIdx, SQFalse);

    // Copy template methods from ::Generic (or specialized template) onto delegate
    sq_pushroottable(vm);
    sq_pushstring(vm, templateName, -1);
    if (SQ_SUCCEEDED(sq_get(vm, -2))) {
        // stack: [userdata, delegate, root, template]
        sq_pushnull(vm);
        while (SQ_SUCCEEDED(sq_next(vm, -2))) {
            // stack: [..., template, iter, key, val]
            // Only copy if key doesn't already exist in delegate
            sq_push(vm, -2);  // key
            sq_rawget(vm, delegateIdx);
            if (sq_gettype(vm, -1) == OT_NULL) {
                // Not in delegate yet — copy
                sq_poptop(vm); // pop null
                sq_push(vm, delegateIdx);
                sq_push(vm, -3); // key
                sq_push(vm, -3); // val
                sq_newslot(vm, -3, SQFalse);
                sq_poptop(vm); // pop delegate ref
            } else {
                sq_poptop(vm); // pop existing value
            }
            sq_pop(vm, 2); // pop key+val from next
        }
        sq_pop(vm, 2); // pop iterator + template
    }
    sq_poptop(vm); // pop root

    // Set delegate on userdata
    // stack: [userdata, delegate]
    sq_setdelegate(vm, -2);
    // stack: [userdata] — ready to return
}

// ── getComponent(name) ────────────────────────────────────

static SQInteger sq_getComponent(HSQUIRRELVM vm) {
    const SQChar *name = nullptr;
    if (sq_gettop(vm) >= 2 && sq_gettype(vm, 2) == OT_STRING) {
        sq_getstring(vm, 2, &name);
    }

    const char *templateName = "Generic";
    if (name) {
        if (strcmp(name, "ResourceManager") == 0) templateName = "ResourceManager";
        else if (strcmp(name, "SoundDriver") == 0) templateName = "SoundDriver";
        else if (strcmp(name, "Display") == 0) templateName = "Display";
    }

    createComponent(vm, templateName);
    return 1;
}

// ── loadFromScript(target, scriptClass) ───────────────────
// If target is userdata, operate on its delegate table.
// If target is table, operate directly (backward compat).

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

    // Determine target: if userdata, get delegate; if table, use directly
    SQInteger targetIdx;
    bool pushedDelegate = false;

    if (sq_gettype(vm, 2) == OT_USERDATA) {
        if (!pushDelegate(vm, 2)) {
            sq_pushbool(vm, SQTrue);
            return 1;
        }
        targetIdx = sq_gettop(vm);
        pushedDelegate = true;
    } else {
        targetIdx = 2;
    }

    // Iterate class and copy members onto target
    sq_push(vm, 3);      // push class
    sq_pushnull(vm);      // push iterator
    while (SQ_SUCCEEDED(sq_next(vm, -2))) {
        sq_push(vm, targetIdx);
        sq_push(vm, -3);  // key
        sq_push(vm, -3);  // val
        sq_newslot(vm, -3, SQFalse);
        sq_poptop(vm);    // pop target ref
        sq_pop(vm, 2);    // pop key+val
    }
    sq_pop(vm, 2);        // pop iterator + class

    if (pushedDelegate) {
        sq_poptop(vm);    // pop delegate
    }

    sq_pushbool(vm, SQTrue);
    return 1;
}

// ── setIdentificator(obj, name) ────────────────────────────
// Works on both userdata (via delegate) and table.

static SQInteger sq_setIdentificator(HSQUIRRELVM vm) {
    if (sq_gettype(vm, 2) == OT_USERDATA) {
        if (!pushDelegate(vm, 2)) return 0;
    } else {
        sq_push(vm, 2);
    }
    sq_pushstring(vm, "__id", -1);
    sq_push(vm, 3);
    sq_rawset(vm, -3);
    sq_poptop(vm);
    return 0;
}

// ── getIdentificator(obj) ──────────────────────────────────

static SQInteger sq_getIdentificator(HSQUIRRELVM vm) {
    if (sq_gettype(vm, 2) == OT_USERDATA) {
        if (!pushDelegate(vm, 2)) {
            sq_pushstring(vm, "unknown", -1);
            return 1;
        }
    } else {
        sq_push(vm, 2);
    }
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
