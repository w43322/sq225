#include <functional>

#include <emscripten/val.h>
#include <squirrel.h>

class SqVMWrapper {
    HSQUIRRELVM vm_ = nullptr;
    emscripten::val logger_ = emscripten::val::global("console")["log"];
    std::vector<emscripten::val> callbacks_;

    static void PrintFunc(HSQUIRRELVM vm, const SQChar *fmt, ...);
    static SQInteger CallNative(HSQUIRRELVM vm);
    emscripten::val SqToVal(int idx);
    void ValToSq(emscripten::val val);

    // Resolve dotted path "A.B.C" → push parent table, return last key.
    // Returns empty string on failure (parent not found).
    std::string ResolvePath(const std::string &path);

public:
    SqVMWrapper();
    ~SqVMWrapper();

    void setLogger(emscripten::val loggerCb);

    // ── Script execution ───────────────────────────────────

    bool eval(const std::string &src, const std::string &name);
    bool loadBytecode(const std::string &src, const std::string &name);

    // ── Unified get/set ────────────────────────────────────
    // Path supports dots: "foo" operates on root table,
    // "System.GetTime" operates on ::System table.
    // set() auto-detects: function → native closure trampoline,
    //                     object (non-null) → new table,
    //                     other → basic type (int/float/bool/string/null).

    void set(const std::string &path, emscripten::val value);
    emscripten::val get(const std::string &path);

    // ── Stack helpers ──────────────────────────────────────

    int getTop();
};
