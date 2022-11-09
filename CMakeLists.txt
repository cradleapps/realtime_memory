cmake_minimum_required(VERSION 3.12)

project("realtime_memory" VERSION 1.0.0
  DESCRIPTION "A header-only backfill for some C++17 pmr memory resources suitable for realtime use."
  HOMEPAGE_URL "https://github.com/cradleapps/memory_resources")

option(REALTIME_MEMORY_TESTS "Enable build of tests" ON)

#------------------------------------------------------------------
# Header-only library
#------------------------------------------------------------------
add_library(realtime_memory INTERFACE)
target_include_directories(realtime_memory INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(realtime_memory INTERFACE cxx_std_17)

if (REALTIME_MEMORY_TESTS)
  message(STATUS "realtime_memory adding tests...")
  add_subdirectory(tests "${CMAKE_BINARY_DIR}/tests")
endif()
