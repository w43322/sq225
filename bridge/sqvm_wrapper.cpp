/**
 * SqVMWrapper — C++ wrapper around Squirrel 2.2.5 C API, exported to JS via Embind.
 *
 * Squirrel source is NOT modified. This wrapper provides a clean class interface
 * so the TS side never touches raw sq_* calls or pointer arithmetic.
 */

#include "sqvm_wrapper.h"

#include <emscripten/bind.h>
#include <sqstdaux.h>
#include <sqstdblob.h>
#include <sqstdio.h>
#include <sqstdmath.h>
#include <sqstdstring.h>

void SqVMWrapper::PrintFunc(HSQUIRRELVM vm, const SQChar *fmt, ...) {
    SqVMWrapper* self = static_cast<SqVMWrapper*>(sq_getforeignptr(vm));
    std::string buff(1024, '\0');
    va_list args;
    va_start(args, fmt);
    va_list args_cpy;
    va_copy(args_cpy, args);
    size_t n = vsnprintf(buff.data(), buff.size(), fmt, args_cpy);
    va_end(args_cpy);
    if (buff.size() < n + 1) {
        buff.resize(n + 1);
        n = vsnprintf(buff.data(), buff.size(), fmt, args);
    }
    va_end(args);
    buff.resize(n);
    if (self->logger_.typeOf().as<std::string>() == "function") {
        self->logger_(buff);
    }
};

SQInteger SqVMWrapper::CallNative(HSQUIRRELVM vm) {
    SqVMWrapper* self = static_cast<SqVMWrapper*>(sq_getforeignptr(vm));
    SQInteger idx;
    if (SQ_FAILED(sq_getinteger(vm, -1, &idx)) || idx < 0 || idx >= self->callbacks_.size()) {
        return sq_throwerror(vm, "invalid callback index");
    }

    int top = sq_gettop(vm) - 1;
    std::vector<emscripten::val> args;
    args.reserve(top - 1);
    for (int i = 2; i <= top; ++i) {
        args.emplace_back(self->SqToVal(i));
    }

    emscripten::val result;
    if (args.size() == 0) {
        result = self->callbacks_[idx]();
    } else if (args.size() == 1) {
        result = self->callbacks_[idx](args[0]);
    } else if (args.size() == 2) {
        result = self->callbacks_[idx](args[0], args[1]);
    } else if (args.size() == 3) {
        result = self->callbacks_[idx](args[0], args[1], args[2]);
    } else {
        result = self->callbacks_[idx].call<emscripten::val>("apply", emscripten::val::null(), emscripten::val(args));
    }

    if (result.isUndefined()) {
        return 0;
    }
    self->ValToSq(result);
    return 1;
}

emscripten::val SqVMWrapper::SqToVal(int idx) {
    switch (sq_gettype(vm_, idx)) {
        case OT_INTEGER: {
            SQInteger v;
            if (SQ_FAILED(sq_getinteger(vm_, idx, &v))) {
                return emscripten::val::undefined();
            }
            return emscripten::val(v);
        }
        case OT_FLOAT: {
            SQFloat v;
            if (SQ_FAILED(sq_getfloat(vm_, idx, &v))) {
                return emscripten::val::undefined();
            }
            return emscripten::val(v);
        }
        case OT_BOOL: {
            SQBool v;
            if (SQ_FAILED(sq_getbool(vm_, idx, &v))) {
                return emscripten::val::undefined();
            }
            return emscripten::val(v == SQTrue);
        }
        case OT_STRING: {
            const SQChar* s;
            if (SQ_FAILED(sq_getstring(vm_, idx, &s))) {
                return emscripten::val::undefined();
            }
            return emscripten::val(s);
        }
        case OT_TABLE: {
            emscripten::val obj = emscripten::val::object();
            sq_push(vm_, idx);       // push table
            sq_pushnull(vm_);        // push iterator
            while (SQ_SUCCEEDED(sq_next(vm_, -2))) {
                // stack: [..., table, iter, key, value]
                obj.set(SqToVal(-2), SqToVal(-1));
                sq_pop(vm_, 2);      // pop key+value
            }
            sq_pop(vm_, 2);          // pop iterator+table
            return obj;
        }
        case OT_ARRAY: {
            SQInteger len = sq_getsize(vm_, idx);
            emscripten::val arr = emscripten::val::global("Array").new_(len);
            for (SQInteger i = 0; i < len; ++i) {
                sq_pushinteger(vm_, i);
                if (SQ_SUCCEEDED(sq_get(vm_, idx < 0 ? idx - 1 : idx))) {
                    arr.set(i, SqToVal(-1));
                    sq_poptop(vm_);
                }
            }
            return arr;
        }
        case OT_NULL: return emscripten::val::null();
        case OT_USERDATA: {
            SQUserPointer p;
            if (SQ_SUCCEEDED(sq_getuserdata(vm_, idx, &p, nullptr))) {
                uint32_t id = *static_cast<uint32_t*>(p);
                emscripten::val obj = emscripten::val::object();
                obj.set("__entityId", emscripten::val(id));
                return obj;
            }
            return emscripten::val::undefined();
        }
        default: return emscripten::val::undefined();
    }
}

void SqVMWrapper::ValToSq(emscripten::val val) {
    std::string jsType = val.typeOf().as<std::string>();
    if (jsType == "boolean") {
        sq_pushbool(vm_, val.as<bool>() ? SQTrue : SQFalse);
    } else if (jsType == "number") {
        double d = val.as<double>();
        int i = static_cast<int>(d);
        if (d == static_cast<double>(i)) {
            sq_pushinteger(vm_, i);
        } else {
            sq_pushfloat(vm_, static_cast<SQFloat>(d));
        }
    } else if (jsType == "string") {
        std::string s = val.as<std::string>();
        sq_pushstring(vm_, s.data(), s.size());
    } else if (jsType == "function") {
        SQInteger idx = static_cast<SQInteger>(callbacks_.size());
        callbacks_.push_back(val);
        sq_pushinteger(vm_, idx);
        sq_newclosure(vm_, CallNative, 1);
    } else if (jsType == "object" && !val.isNull()) {
        bool isArray = emscripten::val::global("Array").call<bool>("isArray", val);
        if (isArray) {
            int len = val["length"].as<int>();
            sq_newarray(vm_, 0);
            for (int i = 0; i < len; ++i) {
                ValToSq(val[i]);
                sq_arrayappend(vm_, -2);
            }
        } else {
            sq_newtable(vm_);
            emscripten::val entries = emscripten::val::global("Object").call<emscripten::val>("entries", val);
            int len = entries["length"].as<int>();
            for (int i = 0; i < len; ++i) {
                emscripten::val entry = entries[i];
                std::string k = entry[0].as<std::string>();
                sq_pushstring(vm_, k.data(), k.size());
                ValToSq(entry[1]);
                sq_newslot(vm_, -3, SQFalse);
            }
        }
    } else {
        sq_pushnull(vm_);
    }
}

// ── Path resolution ───────────────────────────────────────
// "foo"       → pushes root table, returns "foo"
// "A.B"       → pushes ::A on stack, returns "B"
// "A.B.C"     → pushes ::A.B on stack, returns "C"
// On failure  → returns "" (nothing pushed)

std::string SqVMWrapper::ResolvePath(const std::string &path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) {
        // No dot — parent is root table
        sq_pushroottable(vm_);
        return path;
    }

    // Walk the chain: start at root, get each segment
    sq_pushroottable(vm_);
    size_t start = 0;
    while (start < dot) {
        size_t next = path.find('.', start);
        if (next == std::string::npos || next > dot) next = dot;
        std::string seg = path.substr(start, next - start);
        sq_pushstring(vm_, seg.c_str(), seg.size());
        if (SQ_FAILED(sq_get(vm_, -2))) {
            sq_poptop(vm_); // pop whatever is on stack
            return "";
        }
        sq_remove(vm_, -2); // remove parent, keep child
        start = next + 1;
    }

    return path.substr(dot + 1);
}

// ── Constructor / Destructor ──────────────────────────────

SqVMWrapper::SqVMWrapper() {
    vm_ = sq_open(1024);
    sq_setforeignptr(vm_, this);
    sq_setprintfunc(vm_, PrintFunc);
    sqstd_seterrorhandlers(vm_);
    sq_pushroottable(vm_);
    sqstd_register_bloblib(vm_);
    sqstd_register_iolib(vm_);
    sqstd_register_mathlib(vm_);
    sqstd_register_stringlib(vm_);
    sq_poptop(vm_);

    extern void register_engine_api(HSQUIRRELVM vm);
    register_engine_api(vm_);
}

SqVMWrapper::~SqVMWrapper() {
    if (vm_) {
        sq_close(vm_);
        vm_ = nullptr;
    }
}

// ── Logger ────────────────────────────────────────────────

void SqVMWrapper::setLogger(emscripten::val loggerCb) {
    logger_ = loggerCb;
}

// ── Script execution ──────────────────────────────────────

bool SqVMWrapper::eval(const std::string &src, const std::string &name) {
    if (SQ_FAILED(sq_compilebuffer(vm_, src.c_str(), src.size(),
                            name.c_str(), SQTrue))) {
        return false;
    }
    sq_pushroottable(vm_);
    bool ok = SQ_SUCCEEDED(sq_call(vm_, 1, SQFalse, SQTrue));
    sq_poptop(vm_);
    return ok;
}

bool SqVMWrapper::loadBytecode(const std::string &src, const std::string &name) {
    struct ReadCtx {
        const char *ptr;
        int size;
        int offset;
    };
    ReadCtx ctx{src.data(), (int)src.size(), 0};

    auto readFunc = [](SQUserPointer up, SQUserPointer dest, SQInteger size) -> SQInteger {
        auto *ctx = static_cast<ReadCtx *>(up);
        int remaining = ctx->size - ctx->offset;
        int toRead = (size < remaining) ? size : remaining;
        if (toRead <= 0) return 0;
        memcpy(dest, ctx->ptr + ctx->offset, toRead);
        ctx->offset += toRead;
        return toRead;
    };

    if (SQ_FAILED(sq_readclosure(vm_, readFunc, &ctx))) {
        return false;
    }

    sq_pushroottable(vm_);
    bool ok = SQ_SUCCEEDED(sq_call(vm_, 1, SQFalse, SQTrue));
    sq_poptop(vm_);
    return ok;
}

// ── Unified set/get ───────────────────────────────────────

void SqVMWrapper::set(const std::string &path, emscripten::val value) {
    std::string key = ResolvePath(path);
    if (key.empty()) return;
    // stack: [parent table]

    sq_pushstring(vm_, key.c_str(), key.size());
    ValToSq(value);
    sq_newslot(vm_, -3, SQFalse);
    sq_poptop(vm_); // pop parent table
}

emscripten::val SqVMWrapper::get(const std::string &path) {
    std::string key = ResolvePath(path);
    if (key.empty()) return emscripten::val::undefined();
    // stack: [parent table]

    sq_pushstring(vm_, key.c_str(), key.size());
    if (SQ_FAILED(sq_get(vm_, -2))) {
        sq_poptop(vm_); // pop parent
        return emscripten::val::undefined();
    }
    emscripten::val result = SqToVal(-1);
    sq_pop(vm_, 2); // pop value + parent
    return result;
}

// ── Stack helpers ─────────────────────────────────────────

int SqVMWrapper::getTop() {
    return sq_gettop(vm_);
}

// ── Embind ────────────────────────────────────────────────

EMSCRIPTEN_BINDINGS(sqvm_wrapper) {
    emscripten::class_<SqVMWrapper>("SqVMWrapper")
        .constructor()
        .function("setLogger",    &SqVMWrapper::setLogger)
        .function("eval",         &SqVMWrapper::eval)
        .function("loadBytecode", &SqVMWrapper::loadBytecode)
        .function("set",          &SqVMWrapper::set)
        .function("get",          &SqVMWrapper::get)
        .function("getTop",       &SqVMWrapper::getTop);
}
