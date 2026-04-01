cmake_minimum_required(VERSION 3.21)

project(qobuz-qt LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optional D-Bus support
option(USE_DBUS "Use D-Bus integration" ON)

# LTO support
option(USE_LTO "Use link time optimization" OFF)

if (POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif ()

# Find Qt 6
find_package(Qt6 COMPONENTS Core Widgets Network Gui Svg REQUIRED)
if (USE_DBUS)
    find_package(Qt6 OPTIONAL_COMPONENTS DBus QUIET)
endif ()

# ----- Rust backend (cargo) -----
# Don't use find_program — it caches a full path that may differ across machines.
# 'cargo' is resolved via PATH at build time.
set(CARGO_CMD cargo)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CARGO_PROFILE release)
    set(CARGO_PROFILE_FLAG --release)
else()
    set(CARGO_PROFILE debug)
    set(CARGO_PROFILE_FLAG "")
endif()

if(WIN32 AND MSVC)
    set(RUST_LIB "${CMAKE_SOURCE_DIR}/target/${CARGO_PROFILE}/qobuz_backend.lib")
else()
    set(RUST_LIB "${CMAKE_SOURCE_DIR}/target/${CARGO_PROFILE}/libqobuz_backend.a")
endif()

add_custom_target(rust_backend ALL
    COMMAND ${CARGO_CMD} build ${CARGO_PROFILE_FLAG}
        --manifest-path "${CMAKE_SOURCE_DIR}/Cargo.toml"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Building Rust backend (cargo ${CARGO_PROFILE})"
    BYPRODUCTS "${RUST_LIB}"
)

# Imported static library so CMake knows it's a link input
add_library(qobuz_backend_lib STATIC IMPORTED GLOBAL)
set_target_properties(qobuz_backend_lib PROPERTIES
    IMPORTED_LOCATION "${RUST_LIB}"
)
add_dependencies(qobuz_backend_lib rust_backend)

# Create main executable
add_executable(qobuz-qt res.qrc)

# Source files
add_subdirectory(src)

add_dependencies(qobuz-qt rust_backend)

# Include paths
target_include_directories(qobuz-qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/rust/include"
)

# Version / app definitions
target_compile_definitions(qobuz-qt PRIVATE APP_VERSION="v${PROJECT_VERSION}")
target_compile_definitions(qobuz-qt PRIVATE APP_NAME="${PROJECT_NAME}")

# Link Qt + Rust backend
target_link_libraries(qobuz-qt PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Gui
    Qt6::Svg
    qobuz_backend_lib
)

# Platform-specific system libs needed by the Rust audio stack (cpal/backend internals)
if (UNIX AND NOT APPLE)
    target_link_libraries(qobuz-qt PRIVATE asound)
elseif (APPLE)
    find_library(COREAUDIO CoreAudio)
    find_library(AUDIOTOOLBOX AudioToolbox)
    find_library(SECURITY Security)
    find_library(COREFOUNDATION CoreFoundation)
    target_link_libraries(qobuz-qt PRIVATE ${COREAUDIO} ${AUDIOTOOLBOX} ${SECURITY} ${COREFOUNDATION})
elseif (WIN32)
    target_link_libraries(qobuz-qt PRIVATE ws2_32 userenv bcrypt advapi32 ntdll ole32)
endif ()

# Compiler warnings + hardening
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(qobuz-qt PRIVATE
        -Wall -Wextra -Wno-unused-parameter
    )
    if (UNIX AND NOT APPLE)
        target_compile_options(qobuz-qt PRIVATE
            -fstack-protector-strong
            -fPIE
            # _FORTIFY_SOURCE needs optimized builds; in Debug it causes warning spam.
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-D_FORTIFY_SOURCE=2>
            $<$<CONFIG:Debug>:-U_FORTIFY_SOURCE>
        )
        target_link_options(qobuz-qt PRIVATE
            -pie
            -Wl,-z,relro,-z,now
        )
    endif ()
elseif (MSVC)
    target_compile_options(qobuz-qt PRIVATE /W3 /Zc:__cplusplus /permissive-)
endif ()

# D-Bus
if (Qt6DBus_FOUND)
    target_compile_definitions(qobuz-qt PRIVATE USE_DBUS)
    target_link_libraries(qobuz-qt PRIVATE Qt6::DBus)
endif ()

# LTO
if (USE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if (ipo_supported AND CMAKE_BUILD_TYPE STREQUAL "Release")
        set_property(TARGET qobuz-qt PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif ()
endif ()

# Install
if (UNIX AND NOT APPLE)
    install(FILES res/logo/qobuz-qt.svg DESTINATION share/icons/hicolor/scalable/apps)
    install(FILES res/app/qobuz-qt.desktop DESTINATION share/applications)
    install(TARGETS qobuz-qt RUNTIME DESTINATION bin)
endif ()
