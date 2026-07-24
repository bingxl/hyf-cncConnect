add_library(fileio STATIC
    file_io.c
    file_io.h
)

target_include_directories(fileio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
