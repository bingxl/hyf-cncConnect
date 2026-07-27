add_executable(cnc_mazak_test mazak_test.c)

target_link_libraries(cnc_mazak_test
    PRIVATE
        core
)

add_custom_command(TARGET cnc_mazak_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FWLIB_DIR}/Fwlib32.dll"
        "$<TARGET_FILE_DIR:cnc_mazak_test>"
    COMMENT "Copying Fwlib32.dll to output directory"
)
