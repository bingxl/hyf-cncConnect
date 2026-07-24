set(IMGUI_DIR ${PROJECT_SOURCE_DIR}/third_party/imgui)

add_executable(cnc_win32ui WIN32
    # ImGui core
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp

    # ImGui backends
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx9.cpp

    # Application
    imgui_main.cpp
    app.cpp
    app.h
    ui_style.cpp
    ui_style.h
    ui_main_window.cpp
    ui_main_window.h
    ui_machine_mgr.cpp
    ui_machine_mgr.h
    ui_machine_detail.cpp
    ui_machine_detail.h
    ui_history.cpp
    ui_history.h
)

target_include_directories(cnc_win32ui
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

target_compile_definitions(cnc_win32ui PRIVATE
    UNICODE
    _UNICODE
)

target_link_libraries(cnc_win32ui
    PRIVATE
        core
        fileio
        db
        d3d9
        dxguid
)

# Copy Fwlib32.dll to output directory
add_custom_command(TARGET cnc_win32ui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FWLIB_DIR}/Fwlib32.dll"
        "$<TARGET_FILE_DIR:cnc_win32ui>"
    COMMENT "Copying Fwlib32.dll to output directory"
)
