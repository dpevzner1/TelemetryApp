find_package(httplib CONFIG QUIET)
find_package(nlohmann_json CONFIG QUIET)
find_package(spdlog CONFIG QUIET)

set(SERVICE_SOURCES
    main.cpp
    diagnostic_log.cpp
    enterprise_config.cpp
    fleet_heartbeat.cpp
    service_control.cpp
    poll_loop.cpp
    validation.cpp
    api_key_store.cpp
    log_session.cpp
    hardware_registry.cpp
    sensors/cpu.cpp
    sensors/memory.cpp
    sensors/gpu.cpp
    sensors/gpu_nvml.cpp
    sensors/gpu_adl.cpp
    sensors/gpu_igcl.cpp
    sensors/pdh_queries.cpp
    sensors/disk.cpp
    sensors/network.cpp
    sensors/temperatures.cpp
    ipc/shm_writer.cpp
    ipc/pipe_server.cpp
    ipc/http_server.cpp
    process_watcher/watcher.cpp
)

add_executable(telemetry_service ${SERVICE_SOURCES})

target_include_directories(telemetry_service PRIVATE
    "${SHARED_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

set(TELEMETRY_SERVICE_LIBS
    # Windows system libraries
    bcrypt.lib
    ntdll.lib
    pdh.lib
    psapi.lib
    iphlpapi.lib
    wbemuuid.lib
    advapi32.lib
    kernel32.lib
    user32.lib
    ole32.lib
    dxgi.lib
)

if(httplib_FOUND)
    list(APPEND TELEMETRY_SERVICE_LIBS httplib::httplib)
elseif(TELEMETRY_LOCAL_DEPS_DIR)
    target_include_directories(telemetry_service PRIVATE "${TELEMETRY_LOCAL_DEPS_DIR}/httplib")
endif()

if(nlohmann_json_FOUND)
    list(APPEND TELEMETRY_SERVICE_LIBS nlohmann_json::nlohmann_json)
elseif(TELEMETRY_LOCAL_DEPS_DIR)
    target_include_directories(telemetry_service PRIVATE "${TELEMETRY_LOCAL_DEPS_DIR}")
endif()

if(spdlog_FOUND)
    list(APPEND TELEMETRY_SERVICE_LIBS spdlog::spdlog)
elseif(TELEMETRY_LOCAL_DEPS_DIR)
    target_compile_definitions(telemetry_service PRIVATE SPDLOG_HEADER_ONLY)
    target_include_directories(telemetry_service PRIVATE "${TELEMETRY_LOCAL_DEPS_DIR}/spdlog/include")
endif()

target_link_libraries(telemetry_service PRIVATE ${TELEMETRY_SERVICE_LIBS})

set_target_properties(telemetry_service PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    OUTPUT_NAME "telemetry_service"
)

if(MSVC)
    target_link_options(telemetry_service PRIVATE /SUBSYSTEM:CONSOLE)
endif()
