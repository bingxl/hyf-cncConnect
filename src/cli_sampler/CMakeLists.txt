add_executable(cnc_sampler sampler.c)

target_link_libraries(cnc_sampler
    PRIVATE
        core
        fileio
        db
)

# Copy Fwlib32.dll to output directory
add_custom_command(TARGET cnc_sampler POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FWLIB_DIR}/Fwlib32.dll"
        "$<TARGET_FILE_DIR:cnc_sampler>"
    COMMENT "Copying Fwlib32.dll to output directory"
)
