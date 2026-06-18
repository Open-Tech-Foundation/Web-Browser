cmake_minimum_required(VERSION 3.21)
project(otf-browser VERSION 0.1.0 LANGUAGES C CXX)
enable_testing()

# Version string baked into the binary (--version flag + user-agent).
# Priority:
#   1. OTF_VERSION env var — set by CI from the release tag, e.g. "0.1.0-alpha.1".
#   2. package.json "version" — kept in sync by `bun run release`; lets local
#      builds after a release bump show the full pre-release suffix that
#      CMake's project(VERSION) field can't carry (hyphens aren't allowed).
#   3. PROJECT_VERSION — bare semver from the project() line; final fallback.
if(DEFINED ENV{OTF_VERSION})
    set(OTF_VERSION "$ENV{OTF_VERSION}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/package.json")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/package.json" _pkg_json)
    string(JSON _pkg_ver ERROR_VARIABLE _pkg_err GET "${_pkg_json}" version)
    if(_pkg_err OR NOT _pkg_ver)
        set(OTF_VERSION "${PROJECT_VERSION}")
    else()
        set(OTF_VERSION "${_pkg_ver}")
    endif()
else()
    set(OTF_VERSION "${PROJECT_VERSION}")
endif()
message(STATUS "OTF Browser version: ${OTF_VERSION}")

# Set CMake policies
cmake_policy(SET CMP0074 NEW)

# Default to Release build
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# This project expects the CEF SDK to be in third_party/cef
set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef")

# Add CEF to the module search path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)
find_package(SQLite3 REQUIRED)

if(WIN32)
    if(NOT DEFINED ENV{VIPS_ROOT} OR "$ENV{VIPS_ROOT}" STREQUAL "")
        message(FATAL_ERROR "VIPS_ROOT environment variable must be set on Windows")
    endif()
    set(VIPS_ROOT "$ENV{VIPS_ROOT}")
    include_directories(
        "${VIPS_ROOT}/include"
        "${VIPS_ROOT}/include/glib-2.0"
        "${VIPS_ROOT}/lib/glib-2.0/include"
    )
    link_directories("${VIPS_ROOT}/lib")
    set(VIPS_LIBS libvips libglib-2.0 libgobject-2.0)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VIPS REQUIRED vips)
    set(VIPS_LIBS ${VIPS_LIBRARIES})
endif()

# Remove -Werror from CEF standard flags to prevent build failure on warnings with GCC 14
list(REMOVE_ITEM CEF_COMPILER_FLAGS -Werror)
list(REMOVE_ITEM CEF_CXX_COMPILER_FLAGS -Werror)
list(REMOVE_ITEM CEF_LINKER_FLAGS -Wl,--fatal-warnings)

# Include the libcef_dll_wrapper target
add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)

# Application source files
set(OTF_BROWSER_SRCS
    src/main.cc
    src/otf_app.cc
    src/otf_bookmark_runtime.cc
    src/otf_browse_runtime.cc
    src/otf_certificate_runtime.cc
    src/otf_clear_data_rpc.cc
    src/otf_console_rpc.cc
    src/otf_context_menu_runtime.cc
    src/otf_cookie_tracking_rpc.cc
    src/otf_devtools_bridge.cc
    src/otf_doc_preview_rpc.cc
    src/otf_doc_preview_runtime.cc
    src/otf_downloads_rpc.cc
    src/otf_downloads_runtime.cc
    src/otf_find_runtime.cc
    src/otf_findbar_rpc.cc
    src/otf_handler.cc
    src/otf_history_bookmarks_rpc.cc
    src/otf_image_preview_rpc.cc
    src/otf_image_preview_runtime.cc
    src/otf_keyboard_runtime.cc
    src/otf_lifecycle_runtime.cc
    src/otf_message_router_handler.cc
    src/otf_memory_runtime.cc
    src/otf_navigation_rpc.cc
    src/otf_native_rpc.cc
    src/otf_page_runtime.cc
    src/otf_page_policy.cc
    src/otf_permissions_rpc.cc
    src/otf_popup_overlay.cc
    src/otf_popup_runtime.cc
    src/otf_request_context_runtime.cc
    src/otf_rpc_dispatcher.cc
    src/otf_search_rpc.cc
    src/otf_settings_rpc.cc
    src/otf_shutdown_runtime.cc
    src/otf_site_data_rpc.cc
    src/otf_tab_runtime.cc
    src/otf_split_runtime.cc
    src/otf_store.cc
    src/otf_tabs_rpc.cc
    src/otf_ui_rpc.cc
    src/otf_utils.cc
    src/otf_workspaces_rpc.cc
    src/otf_zoom_runtime.cc
)

# Application header files
set(OTF_BROWSER_HEADERS
    include/otf_app.h
    include/otf_bookmark_runtime.h
    include/otf_browse_runtime.h
    include/otf_certificate_runtime.h
    include/otf_clear_data_rpc.h
    include/otf_console_rpc.h
    include/otf_context_menu_runtime.h
    include/otf_cookie_tracking_rpc.h
    include/otf_devtools_bridge.h
    include/otf_doc_preview_rpc.h
    include/otf_doc_preview_runtime.h
    include/otf_downloads_rpc.h
    include/otf_downloads_runtime.h
    include/otf_find_runtime.h
    include/otf_findbar_rpc.h
    include/otf_handler.h
    include/otf_history_bookmarks_rpc.h
    include/otf_image_preview_rpc.h
    include/otf_image_preview_runtime.h
    include/otf_keyboard_runtime.h
    include/otf_lifecycle_runtime.h
    include/otf_message_router_handler.h
    include/otf_memory_runtime.h
    include/otf_navigation_rpc.h
    include/otf_native_rpc.h
    include/otf_page_runtime.h
    include/otf_page_policy.h
    include/otf_permissions_rpc.h
    include/otf_popup_overlay.h
    include/otf_popup_runtime.h
    include/otf_request_context_runtime.h
    include/otf_rpc_dispatcher.h
    include/otf_search_rpc.h
    include/otf_settings_rpc.h
    include/otf_shutdown_runtime.h
    include/otf_site_data_rpc.h
    include/otf_tab_runtime.h
    include/otf_split_runtime.h
    include/otf_browser_shell.h
    include/otf_store.h
    include/otf_tabs_rpc.h
    include/otf_ui_rpc.h
    include/otf_utils.h
    include/otf_workspaces_rpc.h
    include/otf_zoom_runtime.h
)

# Include directories
include_directories(include)
if(NOT WIN32)
    include_directories(${VIPS_INCLUDE_DIRS})
endif()

# Logical target used to link the libcef library
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

# Determine the target output directory
SET_CEF_TARGET_OUT_DIR()

# Create the executable
# WIN32 sets /SUBSYSTEM:WINDOWS on MSVC so the linker looks for wWinMain
# rather than main(). Ignored on non-Windows platforms.
add_executable(otf-browser WIN32 ${OTF_BROWSER_SRCS} ${OTF_BROWSER_HEADERS})

# Apply CEF standard compiler/linker properties
SET_EXECUTABLE_TARGET_PROPERTIES(otf-browser)

# Use C++20 (required by CEF) with appropriate flags per compiler
if(MSVC)
    target_compile_options(otf-browser PRIVATE /std:c++20)
else()
    target_compile_options(otf-browser PRIVATE -std=c++20 -fpermissive -Wno-error)
endif()

# Embed the version so main.cc can surface it via --version and bake it
# into the User-Agent.
target_compile_definitions(otf-browser PRIVATE OTF_VERSION="${OTF_VERSION}")

# Link libraries
target_link_libraries(otf-browser libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS} SQLite::SQLite3 ${VIPS_LIBS})

if(WIN32 AND MSVC)
    # The same executable is used for Chromium renderer/GPU/utility child
    # processes. Keep libvips/GLib out of child-process startup; only the
    # browser process should load them when TIFF/document preview code first
    # calls into libvips.
    target_link_libraries(otf-browser delayimp.lib)
    target_link_options(otf-browser PRIVATE
        "/DELAYLOAD:libvips-42.dll"
        "/DELAYLOAD:libglib-2.0-0.dll"
        "/DELAYLOAD:libgobject-2.0-0.dll"
    )
endif()

# Set rpath so that libraries can be placed next to the executable
if(OS_LINUX)
    set_target_properties(otf-browser PROPERTIES INSTALL_RPATH "$ORIGIN")
    set_target_properties(otf-browser PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE)
endif()

# Copy binary and resource files to the target output directory
COPY_FILES("otf-browser" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES("otf-browser" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")

# Copy non-bundled UI assets (icons, search-engine logos, etc.). Vite owns
# the build/Release/ui directory for HTML + bundled JS (see ui/vite.config.js
# `outDir: '../build/Release/ui'` and `emptyOutDir: true`). Previously this
# step copied the entire raw ui/ source tree on top, which silently clobbered
# vite's bundled HTML — production binaries ended up pointing at
# /src/<page>/main.jsx instead of the hashed bundle. Scope the copy to
# ui/assets so the two outputs coexist without conflicting filenames.
#
# Ordering matters: `bun run build:ui` must run before `bun run build:cpp`
# (vite empties the output dir before writing). package.json's `build` target
# enforces this.
add_custom_command(
    TARGET otf-browser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/ui/assets" "${CEF_TARGET_OUT_DIR}/ui/assets"
    COMMENT "Copying UI static assets (icons, logos)..."
)

# Copy Windows runtime DLLs (vips and its dependencies) next to the executable.
# SQLite3 is statically linked via vcpkg so no DLL is needed for it.
if(WIN32)
    file(GLOB VIPS_DLLS "${VIPS_ROOT}/bin/*.dll")
    foreach(DLL ${VIPS_DLLS})
        add_custom_command(TARGET otf-browser POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" "${CEF_TARGET_OUT_DIR}"
        )
    endforeach()
endif()

# Linux SUID Permission Helper
if(OS_LINUX)
    SET_LINUX_SUID_PERMISSIONS("otf-browser" "${CEF_TARGET_OUT_DIR}/chrome-sandbox")
    
    add_custom_command(
        TARGET otf-browser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "OTF Browser build complete."
        COMMAND ${CMAKE_COMMAND} -E echo "Executable: ${CEF_TARGET_OUT_DIR}/otf-browser"
    )
endif()

add_executable(otf-browser-native-tests
    tests/otf_native_tests.cc
    src/otf_store.cc
    src/otf_utils.cc
)
SET_EXECUTABLE_TARGET_PROPERTIES(otf-browser-native-tests)
target_include_directories(otf-browser-native-tests PRIVATE include)
if(MSVC)
    target_compile_options(otf-browser-native-tests PRIVATE /std:c++20)
else()
    target_compile_options(otf-browser-native-tests PRIVATE -std=c++20 -fpermissive -Wno-error)
endif()
target_link_libraries(otf-browser-native-tests libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS} SQLite::SQLite3 ${VIPS_LIBS})
add_test(NAME otf-browser-native-tests COMMAND otf-browser-native-tests)

add_executable(otf-browser-extra-tests
    tests/otf_extra_tests.cc
    src/otf_utils.cc
    src/otf_page_policy.cc
)
SET_EXECUTABLE_TARGET_PROPERTIES(otf-browser-extra-tests)
target_include_directories(otf-browser-extra-tests PRIVATE include)
if(MSVC)
    target_compile_options(otf-browser-extra-tests PRIVATE /std:c++20)
else()
    target_compile_options(otf-browser-extra-tests PRIVATE -std=c++20 -fpermissive -Wno-error)
endif()
target_link_libraries(otf-browser-extra-tests libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS} SQLite::SQLite3 ${VIPS_LIBS})
add_test(NAME otf-browser-extra-tests COMMAND otf-browser-extra-tests)

add_executable(otf-browser-url-policy-tests
    tests/native/test_url_policy.cc
    src/otf_utils.cc
)
SET_EXECUTABLE_TARGET_PROPERTIES(otf-browser-url-policy-tests)
target_include_directories(otf-browser-url-policy-tests PRIVATE include)
if(MSVC)
    target_compile_options(otf-browser-url-policy-tests PRIVATE /std:c++20)
else()
    target_compile_options(otf-browser-url-policy-tests PRIVATE -std=c++20 -fpermissive -Wno-error)
endif()
target_link_libraries(otf-browser-url-policy-tests libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS} SQLite::SQLite3 ${VIPS_LIBS})
add_test(NAME otf-browser-url-policy-tests COMMAND otf-browser-url-policy-tests)

add_executable(otf-browser-json-bridge-tests
    tests/native/test_json_bridge.cc
    src/otf_utils.cc
)
SET_EXECUTABLE_TARGET_PROPERTIES(otf-browser-json-bridge-tests)
target_include_directories(otf-browser-json-bridge-tests PRIVATE include)
if(MSVC)
    target_compile_options(otf-browser-json-bridge-tests PRIVATE /std:c++20)
else()
    target_compile_options(otf-browser-json-bridge-tests PRIVATE -std=c++20 -fpermissive -Wno-error)
endif()
target_link_libraries(otf-browser-json-bridge-tests libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS} SQLite::SQLite3 ${VIPS_LIBS})
add_test(NAME otf-browser-json-bridge-tests COMMAND otf-browser-json-bridge-tests)

add_executable(otf-browser-tab-manager-tests
    tests/native/test_tab_manager.cc
)
SET_EXECUTABLE_TARGET_PROPERTIES(otf-browser-tab-manager-tests)
target_include_directories(otf-browser-tab-manager-tests PRIVATE include)
if(MSVC)
    target_compile_options(otf-browser-tab-manager-tests PRIVATE /std:c++20)
else()
    target_compile_options(otf-browser-tab-manager-tests PRIVATE -std=c++20 -fpermissive -Wno-error)
endif()
target_link_libraries(otf-browser-tab-manager-tests libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})
add_test(NAME otf-browser-tab-manager-tests COMMAND otf-browser-tab-manager-tests)

add_executable(otf-browser-store-tests
    tests/native/test_store.cc
    src/otf_store.cc
    src/otf_utils.cc
)
SET_EXECUTABLE_TARGET_PROPERTIES(otf-browser-store-tests)
target_include_directories(otf-browser-store-tests PRIVATE include)
if(MSVC)
    target_compile_options(otf-browser-store-tests PRIVATE /std:c++20)
else()
    target_compile_options(otf-browser-store-tests PRIVATE -std=c++20 -fpermissive -Wno-error)
endif()
target_link_libraries(otf-browser-store-tests libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS} SQLite::SQLite3 ${VIPS_LIBS})
add_test(NAME otf-browser-store-tests COMMAND otf-browser-store-tests)

add_executable(otf-browser-doc-preview-tests
    tests/native/test_doc_preview.cc
    src/otf_utils.cc
)
SET_EXECUTABLE_TARGET_PROPERTIES(otf-browser-doc-preview-tests)
target_include_directories(otf-browser-doc-preview-tests PRIVATE include)
if(MSVC)
    target_compile_options(otf-browser-doc-preview-tests PRIVATE /std:c++20)
else()
    target_compile_options(otf-browser-doc-preview-tests PRIVATE -std=c++20 -fpermissive -Wno-error)
endif()
target_link_libraries(otf-browser-doc-preview-tests libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS} ${VIPS_LIBS})
add_test(NAME otf-browser-doc-preview-tests COMMAND otf-browser-doc-preview-tests)

# Single source of truth for "all native tests". CI (release.yml) and the
# package.json test:cpp script both build this one target, so adding a new test
# executable above only needs a line here — never a second list in YAML/json.
add_custom_target(otf-tests DEPENDS
    otf-browser-native-tests
    otf-browser-extra-tests
    otf-browser-url-policy-tests
    otf-browser-json-bridge-tests
    otf-browser-tab-manager-tests
    otf-browser-store-tests
    otf-browser-doc-preview-tests
)
