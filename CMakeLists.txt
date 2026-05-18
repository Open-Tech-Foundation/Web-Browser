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
find_package(PkgConfig REQUIRED)
pkg_check_modules(VIPS REQUIRED vips)

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
    src/otf_handler.cc
    src/otf_page_policy.cc
    src/otf_popup_overlay.cc
    src/otf_store.cc
    src/otf_utils.cc
)

# Application header files
set(OTF_BROWSER_HEADERS
    include/otf_app.h
    include/otf_handler.h
    include/otf_page_policy.h
    include/otf_popup_overlay.h
    include/otf_browser_shell.h
    include/otf_store.h
    include/otf_utils.h
)

# Include directories
include_directories(include ${VIPS_INCLUDE_DIRS})

# Logical target used to link the libcef library
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

# Determine the target output directory
SET_CEF_TARGET_OUT_DIR()

# Create the executable
add_executable(otf-browser ${OTF_BROWSER_SRCS} ${OTF_BROWSER_HEADERS})

# Apply CEF standard compiler/linker properties
SET_EXECUTABLE_TARGET_PROPERTIES(otf-browser)

# Use C++20 (required by CEF) but with permissive flags for GCC 14 compatibility
target_compile_options(otf-browser PRIVATE -std=c++20 -fpermissive -Wno-error)

# Embed the version so main.cc can surface it via --version and bake it
# into the User-Agent.
target_compile_definitions(otf-browser PRIVATE OTF_VERSION="${OTF_VERSION}")

# Link libraries
target_link_libraries(otf-browser libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS} SQLite::SQLite3 ${VIPS_LIBRARIES})

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
target_compile_options(otf-browser-native-tests PRIVATE -std=c++20 -fpermissive -Wno-error)
target_link_libraries(otf-browser-native-tests libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS} SQLite::SQLite3 ${VIPS_LIBRARIES})
add_test(NAME otf-browser-native-tests COMMAND otf-browser-native-tests)

add_executable(otf-browser-extra-tests
    tests/otf_extra_tests.cc
    src/otf_utils.cc
    src/otf_page_policy.cc
)
SET_EXECUTABLE_TARGET_PROPERTIES(otf-browser-extra-tests)
target_include_directories(otf-browser-extra-tests PRIVATE include)
target_compile_options(otf-browser-extra-tests PRIVATE -std=c++20 -fpermissive -Wno-error)
target_link_libraries(otf-browser-extra-tests libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS} SQLite::SQLite3 ${VIPS_LIBRARIES})
add_test(NAME otf-browser-extra-tests COMMAND otf-browser-extra-tests)
