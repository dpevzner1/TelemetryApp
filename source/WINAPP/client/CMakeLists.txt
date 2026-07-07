find_package(nlohmann_json CONFIG QUIET)
find_package(spdlog CONFIG QUIET)
find_package(httplib CONFIG QUIET)

set(CLIENT_SOURCES
    main.cpp
    diagnostic_log.cpp
    window.cpp
    app_icon.rc
    # Renderer
    renderer/d2d_context.cpp
    renderer/waveform_graph.cpp
    renderer/gauge_panel.cpp
    renderer/panel_layout.cpp
    # IPC
    ipc/shm_reader.cpp
    ipc/pipe_client.cpp
    # Config
    config/app_config.cpp
    config/dashboard_profile.cpp
    config/metric_catalog_model.cpp
    # UI
    ui/sidebar.cpp
    ui/compact_overlay.cpp
    ui/system_tray.cpp
    ui/gauge_icon.cpp
    # Pages
    pages/dashboard_page.cpp
    pages/api_page.cpp
    pages/metrics_page.cpp
    pages/fleet_page.cpp
    pages/settings_page.cpp
)

add_executable(telemetry_client WIN32 ${CLIENT_SOURCES})

add_executable(TelemetryApp WIN32 launcher.cpp app_icon.rc)

target_include_directories(telemetry_client PRIVATE
    "${SHARED_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

set(TELEMETRY_CLIENT_LIBS
    # Direct2D / DirectWrite / D3D11
    d2d1.lib
    dwrite.lib
    d3d11.lib
    dxgi.lib
    # Shell / DWM / Common dialogs
    dwmapi.lib
    shell32.lib
    comdlg32.lib
    # Windows core
    kernel32.lib
    user32.lib
    gdi32.lib
    advapi32.lib
    ws2_32.lib
)

if(nlohmann_json_FOUND)
    list(APPEND TELEMETRY_CLIENT_LIBS nlohmann_json::nlohmann_json)
elseif(TELEMETRY_LOCAL_DEPS_DIR)
    target_include_directories(telemetry_client PRIVATE "${TELEMETRY_LOCAL_DEPS_DIR}")
endif()

if(spdlog_FOUND)
    list(APPEND TELEMETRY_CLIENT_LIBS spdlog::spdlog)
elseif(TELEMETRY_LOCAL_DEPS_DIR)
    target_compile_definitions(telemetry_client PRIVATE SPDLOG_HEADER_ONLY)
    target_include_directories(telemetry_client PRIVATE "${TELEMETRY_LOCAL_DEPS_DIR}/spdlog/include")
endif()

if(httplib_FOUND)
    list(APPEND TELEMETRY_CLIENT_LIBS httplib::httplib)
elseif(TELEMETRY_LOCAL_DEPS_DIR)
    target_include_directories(telemetry_client PRIVATE "${TELEMETRY_LOCAL_DEPS_DIR}/httplib")
endif()

target_link_libraries(telemetry_client PRIVATE ${TELEMETRY_CLIENT_LIBS})

target_link_libraries(TelemetryApp PRIVATE kernel32.lib user32.lib)

set_target_properties(telemetry_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    OUTPUT_NAME "telemetry_client"
    WIN32_EXECUTABLE TRUE
)

set_target_properties(TelemetryApp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    OUTPUT_NAME "TelemetryApp"
    WIN32_EXECUTABLE TRUE
)
