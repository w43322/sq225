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
    bridge/sqvm_wrapper.cpp
    bridge/engine_api.cpp
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
    set_target_properties(squirrel_vm PROPERTIES
        SUFFIX ".js"
    )

    target_link_options(squirrel_vm PRIVATE
        "--bind"
        "--emit-tsd" "${CMAKE_CURRENT_BINARY_DIR}/squirrel_vm.d.ts"
        "SHELL:-s WASM=1"
        "SHELL:-s MODULARIZE=1"
        "SHELL:-s EXPORT_ES6=1"
        "SHELL:-s EXPORT_NAME='SquirrelVM'"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s ALLOW_TABLE_GROWTH=1"
        "SHELL:-s NO_EXIT_RUNTIME=1"
        "SHELL:-s ENVIRONMENT='web'"
        "SHELL:-s FILESYSTEM=0"
    )
endif()
