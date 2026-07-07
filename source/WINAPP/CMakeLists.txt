cmake_minimum_required(VERSION 3.24)
project(TelemetryApp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(TELEMETRY_LOCAL_DEPS_DIR "" CACHE PATH "Optional local header dependency root")

# ── vcpkg integration ──────────────────────────────────────────────────────────
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain")
endif()

# ── Windows target ─────────────────────────────────────────────────────────────
if(NOT WIN32)
    message(FATAL_ERROR "TelemetryApp is Windows-only.")
endif()
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00   # Windows 10+
)

# ── MSVC flags ─────────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(
        /utf-8
        /D_CRT_SECURE_NO_WARNINGS
        /W4         # warning level 4
        /WX-        # warnings not errors (relax for third-party headers)
        /MP         # multi-processor compilation
        /MT$<$<CONFIG:Debug>:d>   # static CRT (no runtime DLL dependency)
        /O2         # full optimisation in Release
        /Ob2        # inline expansion
        /GL         # whole-program optimisation (Release)
        /arch:AVX2  # AVX2 intrinsics (Intel Ultra 7 155H supports AVX2)
    )
    add_link_options(/LTCG)  # link-time code generation (pairs with /GL)
endif()

# ── Shared include path ────────────────────────────────────────────────────────
set(SHARED_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shared")

# ── Sub-projects ───────────────────────────────────────────────────────────────
add_subdirectory(service)
add_subdirectory(client)
