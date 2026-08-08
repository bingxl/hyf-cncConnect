cmake_minimum_required(VERSION 3.15)
project(cncConnect
    VERSION 1.0.0
    LANGUAGES C CXX
    DESCRIPTION "CNC Monitor - FOCAS2 Communication Tool"
)

# ---- Global Settings ----
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Incremental build optimizations ----

# ccache: reuse cached compilation results so unchanged TUs don't recompile
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    message(STATUS "ccache found: ${CCACHE_PROGRAM}")
endif()

# MSVC-specific flags
if(MSVC)
    add_compile_options(/W3 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    # Parallel compilation of multiple translation units
    add_compile_options(/MP)
    set(CMAKE_RC_FLAGS "/utf-8")
    # Incremental linking in Debug builds
    foreach(t EXE SHARED MODULE)
        set(CMAKE_${t}_LINKER_FLAGS_DEBUG "${CMAKE_${t}_LINKER_FLAGS_DEBUG} /MANIFEST:NO /INCREMENTAL")
    endforeach()

    # Force MSVC archiver/linker to avoid MSYS2 ar.exe on PATH
    get_filename_component(MSVC_BIN_DIR ${CMAKE_C_COMPILER} DIRECTORY)
    set(CMAKE_AR   "${MSVC_BIN_DIR}/lib.exe" CACHE FILEPATH "" FORCE)
    set(CMAKE_LINKER "${MSVC_BIN_DIR}/link.exe" CACHE FILEPATH "" FORCE)
    set(CMAKE_MT   "${MSVC_BIN_DIR}/mt.exe" CACHE FILEPATH "" FORCE)
    if(NOT CMAKE_RC_COMPILER STREQUAL "${MSVC_BIN_DIR}/rc.exe")
        set(CMAKE_RC_COMPILER "${MSVC_BIN_DIR}/rc.exe" CACHE FILEPATH "")
    endif()
endif()

# ---- FANUC fwlib ----
set(FWLIB_DIR ${PROJECT_SOURCE_DIR}/third_party/fwlib)

# Import Fwlib32 as an imported static library
add_library(fwlib32 STATIC IMPORTED)
set_target_properties(fwlib32 PROPERTIES
    IMPORTED_LOCATION "${FWLIB_DIR}/Fwlib32.lib"
    IMPORTED_IMPLIB   "${FWLIB_DIR}/Fwlib32.lib"
)

# ---- Subdirectories ----
add_subdirectory(src)
add_subdirectory(tests)
