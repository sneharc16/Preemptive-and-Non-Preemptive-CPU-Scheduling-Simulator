cmake_minimum_required(VERSION 3.16)
project(sched VERSION 2.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(SCHED_WERROR "Treat compiler warnings as errors" ON)
option(SCHED_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SCHED_COVERAGE "Build with gcov-style coverage instrumentation" OFF)
option(SCHED_PYTHON_TESTS "Register the Python test suites with CTest" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
# No fused multiply-add: floating-point results (EWMA predictions) must be
# identical on every compiler and match the Python reference simulator.
add_compile_options(-ffp-contract=off)
if(SCHED_WERROR)
  add_compile_options(-Werror)
endif()
if(SCHED_SANITIZE)
  set(SAN_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_compile_options(${SAN_FLAGS} -g)
  add_link_options(${SAN_FLAGS})
endif()
if(SCHED_COVERAGE)
  add_compile_options(--coverage -O0 -g)
  add_link_options(--coverage)
endif()

# Policies are discovered by glob so that adding one is a single new file
# (plus its line in src/policies/registry.c).
file(GLOB SCHED_POLICY_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/policies/*.c)

add_library(schedcore STATIC
  src/engine/engine.c
  src/engine/workload.c
  src/io/input.c
  src/io/output.c
  src/metrics/metrics.c
  src/util/heap.c
  src/util/queue.c
  ${SCHED_POLICY_SOURCES})
target_include_directories(schedcore PUBLIC include PRIVATE src)

add_executable(sched src/cli/main.c)
target_link_libraries(sched PRIVATE schedcore)

enable_testing()

foreach(name util workload input engine output)
  add_executable(test_${name} tests/unit/test_${name}.c)
  target_include_directories(test_${name} PRIVATE src tests/unit)
  target_link_libraries(test_${name} PRIVATE schedcore)
  add_test(NAME unit_${name} COMMAND test_${name})
endforeach()

if(SCHED_PYTHON_TESTS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  foreach(suite golden cases cli differential optimality policies)
    add_test(NAME py_${suite}
      COMMAND ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider tests/test_${suite}.py
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(py_${suite} PROPERTIES ENVIRONMENT "SCHED_BIN=$<TARGET_FILE:sched>")
  endforeach()
endif()
