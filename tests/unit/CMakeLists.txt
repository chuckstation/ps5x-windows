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
  set(SRCS ${ARGN})
  add_executable(test_${NAME} ${SRCS})
  target_link_libraries(test_${NAME} PRIVATE Catch2::Catch2WithMain
                                             ${CHUCKSTATION5_ALL_LIBS})
  target_include_directories(
    test_${NAME} PRIVATE "${CMAKE_SOURCE_DIR}/source/chuckstation5/include"
                         "${CMAKE_BINARY_DIR}/generated")
  target_compile_features(test_${NAME} PRIVATE cxx_std_20)
  chuckstation5_target_compile_options(test_${NAME})
  add_test(NAME chuckstation5::${NAME} COMMAND test_${NAME})
endfunction()

chuckstation5_test(logger         test_logger.cpp)
chuckstation5_test(config         test_config.cpp)
chuckstation5_test(kernel         test_kernel.cpp)
chuckstation5_test(memory         test_memory.cpp)
chuckstation5_test(loader         test_loader.cpp)
chuckstation5_test(audio          test_audio.cpp)
chuckstation5_test(filesystem     test_filesystem.cpp)
chuckstation5_test(gpu            test_gpu.cpp)
chuckstation5_test(kyty_adapter   test_kyty_adapter.cpp)
chuckstation5_test(debugger       test_debugger.cpp)
chuckstation5_test(runtime          test_runtime.cpp)
chuckstation5_test(kernel_runtime   test_kernel_runtime.cpp)
chuckstation5_test(process          test_process.cpp)
chuckstation5_test(execution        test_execution.cpp)
chuckstation5_test(module_registry  test_module_registry.cpp)
chuckstation5_test(kernel_services  test_kernel_services.cpp)
chuckstation5_test(memory_diag      test_memory_diag.cpp)
chuckstation5_test(runtime_events   test_runtime_events.cpp)
chuckstation5_test(dynamic_linker   test_dynamic_linker.cpp)
chuckstation5_test(shader_cache     test_shader_cache.cpp)
chuckstation5_test(perf_tools       test_perf_tools.cpp)

chuckstation5_test(phase6_execution      test_phase6_execution.cpp)
chuckstation5_test(phase6_kernel_runtime test_phase6_kernel_runtime.cpp)
chuckstation5_test(phase6_gpu            test_phase6_gpu.cpp)
chuckstation5_test(phase6_debugger       test_phase6_debugger.cpp)
chuckstation5_test(phase6_filesystem     test_phase6_filesystem.cpp)
chuckstation5_test(phase6_audio            test_phase6_audio.cpp)
chuckstation5_test(phase6_input            test_phase6_input.cpp)
chuckstation5_test(phase6_runtime_events   test_phase6_runtime_events.cpp)
chuckstation5_test(phase6_perf_tools       test_phase6_perf_tools.cpp)
chuckstation5_test(phase6_ui               test_phase6_ui.cpp)

chuckstation5_test(phase7_cpu         test_phase7_cpu.cpp)
chuckstation5_test(phase7_syscalls    test_phase7_syscalls.cpp)
chuckstation5_test(phase7_cmd_proc    test_phase7_cmd_proc.cpp)
chuckstation5_test(phase7_integration test_phase7_integration.cpp)

chuckstation5_test(phase8_cpu         test_phase8_cpu.cpp)
chuckstation5_test(phase8_stability   test_phase8_stability.cpp)
chuckstation5_test(phase8_graphics    test_phase8_graphics.cpp)
chuckstation5_test(phase8_homebrew    test_phase8_homebrew.cpp)
chuckstation5_test(phase8_debugger    test_phase8_debugger.cpp)
chuckstation5_test(phase8_ui          test_phase8_ui.cpp)
chuckstation5_test(phase8_perf        test_phase8_perf.cpp)
chuckstation5_test(phase8_fuzz        test_phase8_fuzz.cpp)
chuckstation5_test(phase8_concurrency test_phase8_concurrency.cpp)
chuckstation5_test(phase8_integration test_phase8_integration.cpp)

chuckstation5_test(phase9_crash_handler test_phase9_crash_handler.cpp)
chuckstation5_test(phase9_metrics       test_phase9_metrics.cpp)
chuckstation5_test(phase9_savestate     test_phase9_savestate.cpp)
chuckstation5_test(phase9_input_mapping test_phase9_input_mapping.cpp)
