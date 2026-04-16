cmake_minimum_required(VERSION 3.15)
project(squirrel_wasm LANGUAGES C CXX)

# ────────────────────────────────────────────────────────────
# Squirrel 2.2.5 (2011) — C++03 era code compiled to WASM
# We do NOT modify any original source files.
# All compatibility fixes are done via compiler flags.
# ────────────────────────────────────────────────────────────

# C++03 — the era this code was written for
set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

# Squirrel VM core
set(SQ_SOURCES
    squirrel/sqapi.cpp
    squirrel/sqbaselib.cpp
    squirrel/sqclass.cpp
    squirrel/sqcompiler.cpp
    squirrel/sqdebug.cpp
    squirrel/sqfuncstate.cpp
    squirrel/sqlexer.cpp
    squirrel/sqmem.cpp
    squirrel/sqobject.cpp
    squirrel/sqstate.cpp
    squirrel/sqtable.cpp
    squirrel/sqvm.cpp
)

# Squirrel standard library
set(SQSTD_SOURCES
    sqstdlib/sqstdaux.cpp
    sqstdlib/sqstdblob.cpp
    sqstdlib/sqstdio.cpp
    sqstdlib/sqstdmath.cpp
    sqstdlib/sqstdrex.cpp
    sqstdlib/sqstdstream.cpp
    sqstdlib/sqstdstring.cpp
    sqstdlib/sqstdsystem.cpp
)

add_executable(squirrel_vm ${SQ_SOURCES} ${SQSTD_SOURCES})

target_include_directories(squirrel_vm PRIVATE
    include
    squirrel
    sqstdlib
)

# ── Compiler compatibility flags ──────────────────────────
#
# 1. The source defines `#define type(obj) ((obj)._type)` in sqobject.h.
#    This collides with `type` used in C++ standard library headers
#    (e.g. <limits>, <type_traits>). We redefine it at compile time.
#
# 2. The code uses functions (sq_vm_free, sq_vm_realloc) inside templates
#    before they are declared. In C++03 with MSVC this was fine (no
#    two-phase lookup). Clang enforces it even in C++03 mode.
#    -fdelayed-template-parsing makes Clang behave like MSVC here.
#
# 3. Suppress warnings for old-style code (format strings, unused values, etc.)

target_compile_options(squirrel_vm PRIVATE
    # Delay template parsing — avoids two-phase lookup errors for
    # sq_vm_free/sq_vm_realloc called in sqvector<T> templates
    -fdelayed-template-parsing

    # Suppress warnings from 2011-era code
    -Wno-format
    -Wno-unused-value
    -Wno-logical-op-parentheses
    -Wno-switch
    -Wno-sometimes-uninitialized
)

# Redefine the `type` macro to avoid collision with C++ stdlib.
# The original: #define type(obj) ((obj)._type)
# We prepend our own define so that when sqobject.h does #define type(obj),
# it's already defined and the redefinition is identical — no conflict.
# But actually we can't prevent the macro from existing.
# Instead, we use a wrapper header that undefs `type` before stdlib includes.
# Simplest approach: just define it as something else at command line,
# then sqobject.h's #define type(obj) will be a redefinition (same).
# ...
# Actually the cleanest fix: the collision only happens because sqvm.cpp
# includes <math.h> AFTER sqobject.h. We need to ensure stdlib headers
# are included BEFORE the macro is defined.
# We can do this with a forced include that pulls in all needed stdlib
# headers before any squirrel header.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sq_prefix.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/sq_prefix.h"
    @ONLY
)

target_compile_options(squirrel_vm PRIVATE
    "-include" "${CMAKE_CURRENT_BINARY_DIR}/sq_prefix.h"
)

if(EMSCRIPTEN)
    # Exported C functions callable from JavaScript
    set(EXPORTED_FUNCTIONS
        "_sq_open"
        "_sq_close"
        "_sq_seterrorhandler"
        "_sq_setcompilererrorhandler"
        "_sq_setprintfunc"
        "_sq_pushroottable"
        "_sq_pushstring"
        "_sq_pushinteger"
        "_sq_pushfloat"
        "_sq_pushbool"
        "_sq_pushnull"
        "_sq_push"
        "_sq_pop"
        "_sq_poptop"
        "_sq_gettop"
        "_sq_settop"
        "_sq_gettype"
        "_sq_getstring"
        "_sq_getinteger"
        "_sq_getfloat"
        "_sq_getbool"
        "_sq_newclosure"
        "_sq_setparamscheck"
        "_sq_newslot"
        "_sq_get"
        "_sq_set"
        "_sq_rawget"
        "_sq_rawset"
        "_sq_newtable"
        "_sq_newarray"
        "_sq_arrayappend"
        "_sq_call"
        "_sq_resume"
        "_sq_readclosure"
        "_sq_writeclosure"
        "_sq_compile"
        "_sq_compilebuffer"
        "_sq_getstackobj"
        "_sq_addref"
        "_sq_release"
        "_sq_createinstance"
        "_sq_newclass"
        "_sq_setclassudsize"
        "_sq_getinstanceup"
        "_sq_setinstanceup"
        "_sq_setnativeclosurename"
        "_sq_throwerror"
        "_sq_getlasterror"
        "_sq_reseterror"
        "_sq_getvmstate"
        "_sq_stackinfos"
        "_sq_tostring"
        "_sq_getsize"
        "_sq_next"
        "_sq_remove"
        "_sq_setfreevariable"
        "_sq_getlocal"
        "_malloc"
        "_free"
    )
    string(JOIN "," EXPORTED_FUNCS_STR ${EXPORTED_FUNCTIONS})

    set_target_properties(squirrel_vm PROPERTIES
        SUFFIX ".js"
    )

    target_link_options(squirrel_vm PRIVATE
        "SHELL:-s WASM=1"
        "SHELL:-s MODULARIZE=1"
        "SHELL:-s EXPORT_NAME='SquirrelVM'"
        "SHELL:-s EXPORTED_FUNCTIONS=[${EXPORTED_FUNCS_STR}]"
        "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','getValue','setValue','UTF8ToString','stringToUTF8','lengthBytesUTF8','addFunction','removeFunction','HEAP8','HEAPU8','HEAP32','HEAPF32']"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s ALLOW_TABLE_GROWTH=1"
        "SHELL:-s NO_EXIT_RUNTIME=1"
        "SHELL:-s ENVIRONMENT='web'"
        "SHELL:-s FILESYSTEM=0"
    )
endif()
