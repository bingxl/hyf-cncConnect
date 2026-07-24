add_library(core STATIC
    cnc_ops.c
    cnc_ops.h
)

target_include_directories(core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${FWLIB_DIR}
)

target_link_libraries(core
    PUBLIC
        fwlib32
        advapi32
)
