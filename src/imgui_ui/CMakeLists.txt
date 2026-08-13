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

    # Entry point
    imgui_main.cpp
    App.cpp
    ui_style.cpp

    # Core (Model layer)
    core/CncData.hpp
    core/CncConnection.hpp
    core/Database.hpp
    core/AsyncData.hpp
    core/StreamingData.hpp
    core/AppState.hpp
    core/IPage.hpp
    core/PageRegistry.hpp

    # ViewModels
    viewmodel/OverviewVm.cpp
    viewmodel/MachineMgrVm.cpp
    viewmodel/MachineDetailVm.cpp
    viewmodel/HistoryVm.cpp
    viewmodel/MachiningVm.cpp

    # Views
    view/MainView.cpp
    view/OverviewView.cpp
    view/MachineMgrView.cpp
    view/MachineDetailView.cpp
    view/HistoryView.cpp
    view/MachiningView.cpp
)

target_include_directories(cnc_win32ui
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/viewmodel
        ${CMAKE_CURRENT_SOURCE_DIR}/view
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
