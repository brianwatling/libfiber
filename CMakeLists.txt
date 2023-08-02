# SPDX-FileCopyrightText: 2023 Brian Watling <brian@oxbo.dev>
# SPDX-License-Identifier: CC0-1.0

cmake_minimum_required(VERSION 3.27)
include(CheckCCompilerFlag)

project(libfiber)

option(FIBER_RUN_TESTS_WITH_BUILD "Whether to run tests as part of the build"
       ON)
option(FIBER_USE_NATIVE_EVENTS "Whether to use the native event engine" ON)
option(FIBER_FAST_SWITCHING "Whether to use assembly context switching" ON)
option(FIBER_ENABLE_ASAN "Whether to enable ASAN checks" OFF)
option(FIBER_ENABLE_TSAN "Whether to enable TSAN checks" OFF)
set(FIBER_STACK_STRATEGY
    "split"
    CACHE STRING "Stack strategy")
set(CMAKE_EXPORT_COMPILE_COMMANDS
    ON
    CACHE BOOL "Export compile commands for VSCode" FORCE)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

check_c_compiler_flag("-fsplit-stack" HAS_SPLIT_STACK)
if((FIBER_STACK_STRATEGY EQUALS "split") AND (NOT HAS_SPLIT_STACK))
  message(WARNING "No split stack support. Falling back to malloc.")
  set(FIBER_STACK_STRATEGY "malloc")
endif()

add_library(fiber)
target_sources(
  fiber
  PRIVATE src/fiber_context.c
          src/fiber_manager.c
          src/fiber_mutex.c
          src/fiber_semaphore.c
          src/fiber_spinlock.c
          src/fiber_cond.c
          src/fiber.c
          src/fiber_barrier.c
          src/fiber_io.c
          src/fiber_rwlock.c
          src/hazard_pointer.c
          src/work_stealing_deque.c
          src/work_queue.c
          src/fiber_scheduler_wsd.c
          $<$<NOT:$<BOOL:FIBER_USE_NATIVE_EVENTS>>:src/fiber_event_ev.c>
          $<$<BOOL:FIBER_USE_NATIVE_EVENTS>:src/fiber_event_native.c>)
target_include_directories(fiber PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(
  fiber
  PUBLIC $<$<STREQUAL:"${FIBER_STACK_STRATEGY}","split">:FIBER_STACK_SPLIT>
         $<$<STREQUAL:"${FIBER_STACK_STRATEGY}","malloc">:FIBER_STACK_MALLOC>
         $<$<STREQUAL:"${FIBER_STACK_STRATEGY}","mmap">:FIBER_STACK_MMAP>
  PRIVATE $<$<BOOL:FIBER_FAST_SWITCHING>:FIBER_FAST_SWITCHING>)
target_compile_options(
  fiber PUBLIC $<$<STREQUAL:"${FIBER_STACK_STRATEGY}","split">:-fsplit-stack>)
target_link_options(
  fiber PUBLIC $<$<STREQUAL:"${FIBER_STACK_STRATEGY}","split">:-fsplit-stack>)

if(FIBER_ENABLE_ASAN)
  target_compile_options(
    fiber
    PUBLIC -fsanitize=address
           -fno-sanitize-recover=address
           -fsanitize=undefined
           -fno-sanitize-recover=undefined
           -fno-omit-frame-pointer
           -fsanitize=float-divide-by-zero
           -fsanitize=float-cast-overflow)
  target_link_options(
    fiber
    PUBLIC
    -fsanitize=address
    -fno-sanitize-recover=address
    -fsanitize=undefined
    -fno-sanitize-recover=undefined
    -fno-omit-frame-pointer
    -fsanitize=float-divide-by-zero
    -fsanitize=float-cast-overflow)
  target_link_options(fiber PUBLIC -static-libasan)
elseif(FIBER_ENABLE_TSAN)
  target_compile_options(
    fiber PUBLIC -fsanitize=thread -fno-sanitize-recover=thread
                 -fno-omit-frame-pointer)
  target_link_options(fiber PUBLIC -fsanitize=thread
                      -fno-sanitize-recover=thread -fno-omit-frame-pointer)
endif()

enable_testing()

macro(fibertest test_name)
  add_executable(${test_name} test/${test_name}.c)
  target_link_libraries(${test_name} fiber)
  foreach(dep_library ${ARGN})
    target_link_libraries(${test_name} ${dep_library})
  endforeach(dep_library ${ARGN})
  set(fiber_unit_tests ${fiber_unit_tests} ${test_name})
  add_test(fibertest_${test_name} ${test_name})
  if(FIBER_ENABLE_ASAN)
    set_property(
      TEST fibertest_${test_name}
      PROPERTY
        ENVIRONMENT
        "ASAN_OPTIONS=symbolize=1:detect_leaks=1;LSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/leak.supp:fast_unwind_on_malloc=0"
    )
  endif()
  if(FIBER_ENABLE_TSAN)
    set_property(TEST fibertest_${test_name}
                 PROPERTY ENVIRONMENT "TSAN_OPTIONS=second_deadlock_stack=1")
  endif()
  if(FIBER_RUN_TESTS_WITH_BUILD)
    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test_output_${test_name}
      DEPENDS ${test_name}
      COMMAND
        ${CMAKE_CTEST_COMMAND} ARGS -R ^fibertest_${test_name}$$
        --output-on-failure && echo success >
        ${CMAKE_CURRENT_BINARY_DIR}/test_output_${test_name}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    add_custom_target(
      run_${test_name} ALL
      DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/test_output_${test_name}
      COMMAND echo -n)
  endif()
endmacro()

fibertest(test_tryjoin)
fibertest(test_sleep)
fibertest(test_io)
fibertest(test_context)
fibertest(test_context_speed)
fibertest(test_basic)
fibertest(test_multithread)
fibertest(test_mpmc_stack)
fibertest(test_mpmc_fifo)
fibertest(test_spsc)
fibertest(test_mpsc)
fibertest(test_mpscr)
fibertest(test_wsd)
fibertest(test_mutex)
fibertest(test_semaphore)
fibertest(test_wait_in_queue)
fibertest(test_cond)
fibertest(test_barrier)
fibertest(test_spinlock)
fibertest(test_rwlock)
fibertest(test_hazard_pointers)
fibertest(test_lockfree_ring_buffer)
fibertest(test_lockfree_ring_buffer2)
fibertest(test_unbounded_channel)
fibertest(test_channel_pingpong)
fibertest(test_unbounded_channel_pingpong)
fibertest(test_work_queue)
fibertest(test_yield_speed)
fibertest(test_dist_fifo)
fibertest(test_wsd_scale)
fibertest(test_multi_channel)
fibertest(test_bounded_mpmc_channel)
fibertest(test_bounded_mpmc_channel2)
fibertest(test_channel)
fibertest(test_pthread_cond)
