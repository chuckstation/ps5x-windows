cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/catch2/catch_amalgamated.hpp")
  add_library(Catch2 INTERFACE)
  target_include_directories(Catch2
                             INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/catch2")
else()
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4)
  FetchContent_MakeAvailable(Catch2)
endif()

set(PS5X_ALL_LIBS
    ps5x_logger
    ps5x_config
    ps5x_memory
    ps5x_kernel
    ps5x_loader
    ps5x_audio
    ps5x_filesystem
    ps5x_gpu
    ps5x_renderer
    ps5x_debugger
    ps5x_kyty_adapter
    ps5x_runtime
    ps5x_kernel_runtime
    ps5x_process
    ps5x_input
    ps5x_execution
    ps5x_module_registry
    ps5x_kernel_services
    ps5x_memory_diag
    ps5x_runtime_events
    ps5x_dynamic_linker
    ps5x_shader_cache
    ps5x_perf_tools
    ps5x_cpu
    ps5x_syscalls
    ps5x_command_processor
    ps5x_crash_handler
    ps5x_metrics
    ps5x_save_state
    ps5x_input_mapping)

function(ps5x_test NAME)
  add_executable(test_${NAME} ${ARGN})
  target_link_libraries(test_${NAME} PRIVATE Catch2::Catch2WithMain
                                             ${PS5X_ALL_LIBS})
  target_include_directories(
    test_${NAME} PRIVATE "${CMAKE_SOURCE_DIR}/source/ps5x/include"
                         "${CMAKE_BINARY_DIR}/generated")
  target_compile_features(test_${NAME} PRIVATE cxx_std_20)
  add_test(NAME ps5x::${NAME} COMMAND test_${NAME})
endfunction()

add_subdirectory(unit)
