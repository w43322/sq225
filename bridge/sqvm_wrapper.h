#include <functional>

#include <emscripten/val.h>
#include <squirrel.h>

class SqVMWrapper {
    HSQUIRRELVM vm_ = nullptr;
    emscripten::val logger_;
    std::vector<emscripten::val> callbacks_;

    static void PrintFunc(HSQUIRRELVM vm, const SQChar *fmt, ...);
    static SQInteger CallNative(HSQUIRRELVM vm);
    emscripten::val SqToVal(int idx);
    void ValToSq(emscripten::val val);

public:
    SqVMWrapper();
    ~SqVMWrapper();

    // ── Logger ─────────────────────────────────────────────

    void setLogger(emscripten::val loggerCb);

    // ── Script execution ───────────────────────────────────

    bool eval(const std::string &src, const std::string &name);

    bool loadBytecode(const std::string &src, const std::string &name);

    // ── Function registration ──────────────────────────────

    void registerGlobalFunc(const std::string &name, emscripten::val func);

    // ── Global variables ───────────────────────────────────

    void setGlobalVar(const std::string &name, emscripten::val value);
    emscripten::val getGlobalVar(const std::string &key);

    // ── Stack helpers (for use inside callbacks) ───────────

    int getTop();
};
