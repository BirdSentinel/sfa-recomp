# snupi - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(snupi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(SNUPI_SOURCES
    src/main.cpp
    src/guest_fixes.cpp
    src/skip_intro.cpp
    src/internal_resolution.cpp
    src/display_settings.cpp
    src/frame_limiter.cpp
    src/vsync_settings.cpp
    src/window_resize.cpp
)

if(WIN32)
    enable_language(RC)
    add_executable(snupi WIN32 ${SNUPI_SOURCES} resources/snupi.rc)
    target_include_directories(snupi PRIVATE resources)
else()
    add_executable(snupi ${SNUPI_SOURCES})
endif()
set_target_properties(snupi PROPERTIES OUTPUT_NAME snoopy)
target_link_libraries(snupi PRIVATE comctl32)

# Always build the game's RTV-only renderer, never stage the generic SDK plugin.
if(NOT WIN32)
  message(FATAL_ERROR "Snoopy requires the Windows DirectX 12 RTV renderer")
endif()
unset(SNUPI_PATCHED_XENOS CACHE)
unset(SNUPI_NATIVE_UI CACHE)
add_subdirectory(renderer)
add_library(rexgpu-xenos ALIAS snupi_xenos)

rexglue_setup_target(snupi GPU_PLUGINS xenos)

# Keep the SDK lifecycle with a game-specific command-line alias.
get_target_property(SNUPI_APP_SOURCES snupi SOURCES)
list(REMOVE_ITEM SNUPI_APP_SOURCES "${REXGLUE_SHARE_DIR}/windowed_app_main_sdl.cpp")
set_property(TARGET snupi PROPERTY SOURCES ${SNUPI_APP_SOURCES})
target_sources(snupi PRIVATE src/app_entry.cpp)
target_include_directories(snupi PRIVATE "${REXGLUE_SHARE_DIR}")

option(SNUPI_BUILD_STARTUP_TESTS "Build startup option and intro package tests" OFF)
if(SNUPI_BUILD_STARTUP_TESTS)
  enable_testing()
  add_executable(snupi_startup_test tests/startup_test.cpp)
  add_executable(snupi_display_guest_test tests/display_guest_test.cpp)
  target_include_directories(snupi_display_guest_test PRIVATE src)
  add_test(NAME snupi_display_guest COMMAND snupi_display_guest_test)
  add_executable(snupi_frame_limiter_test tests/frame_limiter_test.cpp)
  target_include_directories(snupi_frame_limiter_test PRIVATE src)
  add_test(NAME snupi_frame_limiter COMMAND snupi_frame_limiter_test)
  add_executable(snupi_presentation_policy_test tests/presentation_policy_test.cpp)
  target_include_directories(snupi_presentation_policy_test PRIVATE renderer)
  add_test(NAME snupi_presentation_policy COMMAND snupi_presentation_policy_test)
  add_executable(snupi_main_menu_test tests/main_menu_test.cpp)
  target_include_directories(snupi_main_menu_test PRIVATE src)
  add_test(NAME snupi_main_menu COMMAND snupi_main_menu_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extracted/Packages/UI/FrontEnd.pkg"
    "${CMAKE_CURRENT_BINARY_DIR}/test-hidelive.pkg")
  add_test(NAME snupi_pause_menu COMMAND snupi_main_menu_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extracted/Packages/UI/InGame.pkg"
    "${CMAKE_CURRENT_BINARY_DIR}/test-hidelive-pause.pkg" pause)
  target_include_directories(snupi_startup_test PRIVATE src)
  add_test(NAME snupi_startup COMMAND snupi_startup_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extracted/Packages/UI/InitialLoadingInterface.pkg"
    "${CMAKE_CURRENT_BINARY_DIR}/test-intro.pkg")
endif()

# A renderer-only change need not relink snoopy.exe. Stage it on every build.
add_custom_target(snupi_xenos_runtime
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:snupi_xenos>" "$<TARGET_FILE_DIR:snupi>"
    DEPENDS snupi_xenos VERBATIM)
add_dependencies(snupi snupi_xenos_runtime)

# Apply the SDK 0.10.0 vector-exp edge-case correction after every codegen.
add_custom_target(snupi_math_codegen_fix
    COMMAND ${CMAKE_COMMAND}
        -DGENERATED_DIR=${CMAKE_CURRENT_SOURCE_DIR}/generated/default
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FixVectorExp.cmake
    DEPENDS snupi_codegen
    VERBATIM
)
add_dependencies(snupi_recomp snupi_math_codegen_fix)

# The original development workspace maintains a clean source export for Git.
# Standalone clones have no sibling git/ export and need no mirroring step.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../git/.repo-export.json")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_custom_target(snupi_repo_sync ALL
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/sync_repo.py"
    DEPENDS snupi
    COMMENT "Updating source-only Git export"
    VERBATIM)
endif()

