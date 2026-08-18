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

set(CHUCKSTATION5_ALL_LIBS
    chuckstation5_logger
    chuckstation5_config
    chuckstation5_memory
    chuckstation5_kernel
    chuckstation5_loader
    chuckstation5_audio
    chuckstation5_filesystem
    chuckstation5_gpu
    chuckstation5_renderer
    chuckstation5_debugger
    chuckstation5_kyty_adapter
    chuckstation5_runtime
    chuckstation5_kernel_runtime
    chuckstation5_process
    chuckstation5_input
    chuckstation5_execution
    chuckstation5_module_registry
    chuckstation5_kernel_services
    chuckstation5_memory_diag
    chuckstation5_runtime_events
    chuckstation5_dynamic_linker
    chuckstation5_shader_cache
    chuckstation5_perf_tools
    chuckstation5_cpu
    chuckstation5_syscalls
    chuckstation5_command_processor
    chuckstation5_crash_handler
    chuckstation5_metrics
    chuckstation5_save_state
    chuckstation5_input_mapping)

function(chuckstation5_test NAME)
  add_executable(test_${NAME} ${ARGN})
  target_link_libraries(test_${NAME} PRIVATE Catch2::Catch2WithMain
                                             ${CHUCKSTATION5_ALL_LIBS})
  target_include_directories(
    test_${NAME} PRIVATE "${CMAKE_SOURCE_DIR}/source/chuckstation5/include"
                         "${CMAKE_BINARY_DIR}/generated")
  target_compile_features(test_${NAME} PRIVATE cxx_std_20)
  add_test(NAME chuckstation5::${NAME} COMMAND test_${NAME})
endfunction()

add_subdirectory(unit)
