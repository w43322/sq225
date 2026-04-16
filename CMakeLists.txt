cmake_minimum_required(VERSION 3.15)
project(squirrel_wasm LANGUAGES C CXX)

# Squirrel 2.2.5 source is NOT modified.
# All compatibility fixes are done via compiler flags only.

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

add_executable(squirrel_vm ${SQ_SOURCES} ${SQSTD_SOURCES}
    bridge/sq_bridge_print.cpp
)

target_include_directories(squirrel_vm PRIVATE
    include
    squirrel
    sqstdlib
)

# -fdelayed-template-parsing: sqvector<T> calls sq_vm_free/sq_vm_realloc
# before they are declared; Clang's two-phase lookup rejects this without it.
# -include limits: sqobject.h's `#define type(obj)` collides with <limits>;
# pre-including it ensures the header is parsed before the macro is defined.
target_compile_options(squirrel_vm PRIVATE
    -fdelayed-template-parsing
    "-include" "limits"
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
        # Squirrel standard library registration
        "_sqstd_register_mathlib"
        "_sqstd_register_stringlib"
        "_sqstd_register_bloblib"
        "_sqstd_seterrorhandlers"
        # C bridge: variadic printf wrapper + one-call setup
        "_sq_bridge_setup"
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
