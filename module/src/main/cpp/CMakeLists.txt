cmake_minimum_required(VERSION 3.22)
project(il2cppdumper LANGUAGES C CXX)
if(NOT DEFINED MODULE_NAME)
    set(MODULE_NAME il2cppdumper)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(xdl STATIC xdl/xdl.c xdl/xdl_iterate.c xdl/xdl_linker.c xdl/xdl_lzma.c xdl/xdl_util.c)
set_target_properties(xdl PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 17)
target_include_directories(xdl PUBLIC xdl/include)
target_compile_options(xdl PRIVATE -Wall -Wextra -Wformat=2 -fvisibility=hidden)

add_library(${MODULE_NAME} SHARED main.cpp hack.cpp discovery.cpp il2cpp_api.cpp il2cpp_dump.cpp
    core/elf.cpp core/runtime_layout.cpp core/maps.cpp core/target.cpp core/output.cpp)
target_compile_options(${MODULE_NAME} PRIVATE -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Werror
    -fno-exceptions -fno-rtti -fvisibility=hidden -fvisibility-inlines-hidden
    -ffunction-sections -fdata-sections)
target_link_options(${MODULE_NAME} PRIVATE -Wl,--exclude-libs,ALL -Wl,--gc-sections
    -Wl,--hash-style=both -Wl,-z,max-page-size=16384
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
set_property(TARGET ${MODULE_NAME} APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
target_link_libraries(${MODULE_NAME} PRIVATE xdl log dl)
if(DEFINED GAME_PACKAGE_NAME)
    target_compile_definitions(${MODULE_NAME} PRIVATE GAME_PACKAGE_NAME="${GAME_PACKAGE_NAME}")
endif()
