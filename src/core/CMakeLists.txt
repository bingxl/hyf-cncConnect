add_library(core STATIC
    cnc_ops.c
    cnc_ops.h
    mazak_ops.c
    mazak_ops.h
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
        ws2_32
        iphlpapi
)
