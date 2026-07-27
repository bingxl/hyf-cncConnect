# Core libraries
add_subdirectory(core)
add_subdirectory(fileio)
add_subdirectory(db)

# CLI executables
add_subdirectory(cli_monitor)
add_subdirectory(cli_collector)
add_subdirectory(cli_mazaktest)

# ImGui-based Win32 UI
add_subdirectory(imgui_ui)
