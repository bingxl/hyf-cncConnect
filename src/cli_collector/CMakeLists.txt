add_executable(cnc_collect collect.c)

target_link_libraries(cnc_collect
    PRIVATE
        core
        fileio
)

# Copy Fwlib32.dll to output directory
add_custom_command(TARGET cnc_collect POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FWLIB_DIR}/Fwlib32.dll"
        "$<TARGET_FILE_DIR:cnc_collect>"
    COMMENT "Copying Fwlib32.dll to output directory"
)
