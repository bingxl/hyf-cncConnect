add_executable(cnc_monitor main.c)

target_link_libraries(cnc_monitor
    PRIVATE
        core
)

# Copy Fwlib32.dll to output directory
add_custom_command(TARGET cnc_monitor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FWLIB_DIR}/Fwlib32.dll"
        "$<TARGET_FILE_DIR:cnc_monitor>"
    COMMENT "Copying Fwlib32.dll to output directory"
)
