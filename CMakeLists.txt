cmake_minimum_required(VERSION 3.15)
project(cncConnect
    VERSION 1.0.0
    LANGUAGES C CXX
    DESCRIPTION "CNC Monitor - FOCAS2 Communication Tool"
)

# ---- Global Settings ----
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC-specific flags
if(MSVC)
    add_compile_options(/W3 /O2 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS ONO8D)
    set(CMAKE_RC_FLAGS "/utf-8")
endif()

# ---- FANUC fwlib ----
set(FWLIB_DIR ${PROJECT_SOURCE_DIR}/fwlib)

# Import Fwlib32 as an imported static library
add_library(fwlib32 STATIC IMPORTED)
set_target_properties(fwlib32 PROPERTIES
    IMPORTED_LOCATION "${FWLIB_DIR}/Fwlib32.lib"
    IMPORTED_IMPLIB   "${FWLIB_DIR}/Fwlib32.lib"
)

# ---- Subdirectories ----
add_subdirectory(src)
add_subdirectory(tests)
