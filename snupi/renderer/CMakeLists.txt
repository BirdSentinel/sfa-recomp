cmake_minimum_required(VERSION 3.25)
project(SnupiRtvRenderer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(rexglue CONFIG REQUIRED)
set(RENDERER_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../tools/rexglue-sdk-terrain" CACHE PATH "Patched SDK v0.10.0 source")
add_library(snupi_xenos SHARED
  ${RENDERER_SOURCE}/src/graphics/plugin_main.cpp
  ${RENDERER_SOURCE}/src/graphics/flags.cpp
  ${RENDERER_SOURCE}/src/graphics/register_file.cpp
  ${RENDERER_SOURCE}/src/graphics/registers.cpp
  ${RENDERER_SOURCE}/src/graphics/pipeline/shader/shader.cpp
  ${RENDERER_SOURCE}/src/graphics/format/ucode.cpp
  ${RENDERER_SOURCE}/src/graphics/sampler_info.cpp
  ${RENDERER_SOURCE}/src/graphics/graphics_system.cpp
  ${RENDERER_SOURCE}/src/graphics/command_processor.cpp
  ${RENDERER_SOURCE}/src/graphics/util/draw_extent_estimator.cpp
  ${RENDERER_SOURCE}/src/graphics/util/draw.cpp
  ${RENDERER_SOURCE}/src/graphics/packet_disassembler.cpp
  ${RENDERER_SOURCE}/src/graphics/primitive_processor.cpp
  ${RENDERER_SOURCE}/src/graphics/pipeline/render_target/cache.cpp
  ${RENDERER_SOURCE}/src/graphics/shared_memory.cpp
  ${RENDERER_SOURCE}/src/graphics/pipeline/texture/cache.cpp
  ${RENDERER_SOURCE}/src/graphics/pipeline/shader/interpreter.cpp
  ${RENDERER_SOURCE}/src/graphics/pipeline/shader/translator.cpp
  ${RENDERER_SOURCE}/src/graphics/pipeline/shader/translator_disasm.cpp
  ${RENDERER_SOURCE}/thirdparty/dxbc/DXBCChecksum.cpp
  ${RENDERER_SOURCE}/src/graphics/pipeline/shader/dxbc.cpp
  ${RENDERER_SOURCE}/src/graphics/pipeline/shader/dxbc_translator.cpp
  ${RENDERER_SOURCE}/src/graphics/pipeline/shader/dxbc_translator_alu.cpp
  ${RENDERER_SOURCE}/src/graphics/pipeline/shader/dxbc_translator_fetch.cpp
  ${RENDERER_SOURCE}/src/graphics/pipeline/shader/dxbc_translator_memexport.cpp
  ${RENDERER_SOURCE}/src/graphics/pipeline/shader/dxbc_translator_om.cpp
  ${RENDERER_SOURCE}/src/graphics/d3d12/graphics_system.cpp
  ${RENDERER_SOURCE}/src/graphics/d3d12/command_processor.cpp
  ${RENDERER_SOURCE}/src/graphics/d3d12/primitive_processor.cpp
  ${RENDERER_SOURCE}/src/graphics/d3d12/render_target_cache.cpp
  ${RENDERER_SOURCE}/src/graphics/d3d12/shader.cpp
  ${RENDERER_SOURCE}/src/graphics/d3d12/shared_memory.cpp
  ${RENDERER_SOURCE}/src/graphics/d3d12/texture_cache.cpp
  ${RENDERER_SOURCE}/src/graphics/d3d12/deferred_command_list.cpp
  ${RENDERER_SOURCE}/src/graphics/d3d12/pipeline_cache.cpp
)
set_target_properties(snupi_xenos PROPERTIES OUTPUT_NAME rexgpu-xenos DEBUG_POSTFIX d RELWITHDEBINFO_POSTFIX rd)
target_include_directories(snupi_xenos BEFORE PRIVATE "${RENDERER_SOURCE}/include" "${RENDERER_SOURCE}" "${RENDERER_SOURCE}/thirdparty/renderdoc")
target_link_libraries(snupi_xenos PRIVATE rex::runtime rex::xxhash d3d12 dxgi dxguid)
target_compile_definitions(snupi_xenos PRIVATE REX_PLATFORM_WINDOWS=1 REX_HAS_D3D12=1 REX_HAS_VULKAN=0 SNUPI_RTV_ONLY=1)
target_compile_options(snupi_xenos PRIVATE -fno-strict-aliasing -ffp-model=strict -fno-char8_t -Wno-unused-variable)
target_compile_options(snupi_xenos PRIVATE -msse4.1)


target_include_directories(snupi_xenos PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

if(PROJECT_IS_TOP_LEVEL)
  include(CTest)
endif()
if(BUILD_TESTING)
  get_target_property(DEPTH_SHADER_SOURCES snupi_xenos SOURCES)
  list(FILTER DEPTH_SHADER_SOURCES INCLUDE REGEX "/(pipeline/shader/|format/ucode\\.cpp|flags\\.cpp|thirdparty/dxbc/)")
  add_executable(depth_bias_gpu_test depth_bias_gpu_test.cpp ${DEPTH_SHADER_SOURCES}
    ${RENDERER_SOURCE}/src/graphics/util/draw.cpp
    ${RENDERER_SOURCE}/src/graphics/register_file.cpp
    ${RENDERER_SOURCE}/src/graphics/registers.cpp)
  target_include_directories(depth_bias_gpu_test BEFORE PRIVATE "${RENDERER_SOURCE}/include" "${RENDERER_SOURCE}" "${RENDERER_SOURCE}/thirdparty/renderdoc")
  target_link_libraries(depth_bias_gpu_test PRIVATE rex::runtime rex::xxhash d3d12 dxgi dxguid d3dcompiler)
  target_compile_definitions(depth_bias_gpu_test PRIVATE REX_PLATFORM_WINDOWS=1 REX_HAS_D3D12=1)
  target_compile_options(depth_bias_gpu_test PRIVATE -fno-strict-aliasing -ffp-model=strict -fno-char8_t -msse4.1)
  add_test(NAME depth_bias_gpu COMMAND depth_bias_gpu_test)
  set_tests_properties(depth_bias_gpu PROPERTIES TIMEOUT 60
    ENVIRONMENT "PATH=${CMAKE_CURRENT_LIST_DIR}/../../win-amd64/bin\;$ENV{PATH}")
  add_executable(geometry_depth_test geometry_depth_test.cpp)
  target_include_directories(geometry_depth_test BEFORE PRIVATE "${RENDERER_SOURCE}/include")
  target_link_libraries(geometry_depth_test PRIVATE rex::runtime)
  target_compile_definitions(geometry_depth_test PRIVATE REX_PLATFORM_WINDOWS=1)
  target_compile_options(geometry_depth_test PRIVATE -fno-char8_t -msse4.1)
  add_test(NAME geometry_depth_consistency COMMAND geometry_depth_test)
  set_tests_properties(geometry_depth_consistency PROPERTIES
    ENVIRONMENT "PATH=${CMAKE_CURRENT_LIST_DIR}/../../win-amd64/bin\;$ENV{PATH}")
  add_executable(viewport_depth_test viewport_depth_test.cpp
    ${RENDERER_SOURCE}/src/graphics/util/draw.cpp
    ${RENDERER_SOURCE}/src/graphics/register_file.cpp
    ${RENDERER_SOURCE}/src/graphics/registers.cpp)
  target_include_directories(viewport_depth_test BEFORE PRIVATE "${RENDERER_SOURCE}/include")
  target_link_libraries(viewport_depth_test PRIVATE rex::runtime)
  target_compile_definitions(viewport_depth_test PRIVATE REX_PLATFORM_WINDOWS=1)
  target_compile_options(viewport_depth_test PRIVATE -ffunction-sections -fdata-sections -ffp-model=strict -fno-char8_t -msse4.1)
  target_link_options(viewport_depth_test PRIVATE -Wl,/OPT:REF)
  add_test(NAME viewport_depth_formats COMMAND viewport_depth_test)
  set_tests_properties(viewport_depth_formats PROPERTIES
    ENVIRONMENT "PATH=${CMAKE_CURRENT_LIST_DIR}/../../win-amd64/bin\;$ENV{PATH}")
endif()
