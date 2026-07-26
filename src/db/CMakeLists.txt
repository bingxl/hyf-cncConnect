set(SQLITE_DIR ${PROJECT_SOURCE_DIR}/third_party/sqlite3)

add_library(db STATIC
    db_ops.c
    db_ops.h
    ${SQLITE_DIR}/sqlite3.c
    ${SQLITE_DIR}/sqlite3.h
)

target_include_directories(db
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${SQLITE_DIR}
)

target_compile_definitions(db PRIVATE
    _CRT_SECURE_NO_WARNINGS
    SQLITE_THREADSAFE=0
)
