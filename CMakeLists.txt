cmake_minimum_required(VERSION 3.21)
project(otf-browser)

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
    src/otf_utils.cc
)

# Application header files
set(OTF_BROWSER_HEADERS
    include/otf_app.h
    include/otf_handler.h
    include/otf_browser_shell.h
    include/otf_utils.h
)

# Include directories
include_directories(include)

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

# Link libraries
target_link_libraries(otf-browser libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})

# Set rpath so that libraries can be placed next to the executable
if(OS_LINUX)
    set_target_properties(otf-browser PROPERTIES INSTALL_RPATH "$ORIGIN")
    set_target_properties(otf-browser PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE)
endif()

# Copy binary and resource files to the target output directory
COPY_FILES("otf-browser" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES("otf-browser" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")

# Copy custom UI resources
add_custom_command(
    TARGET otf-browser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/ui" "${CEF_TARGET_OUT_DIR}/ui"
    COMMENT "Copying UI resources..."
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
