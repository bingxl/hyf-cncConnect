add_library(db STATIC
    db_ops.c
    db_ops.h
    sqlite3.c
    sqlite3.h
)

target_include_directories(db
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(db PRIVATE
    _CRT_SECURE_NO_WARNINGS
    SQLITE_THREADSAFE=0
)
