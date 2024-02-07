cmake_minimum_required(VERSION 3.12)

#------------------------------------------------------------------
# Tests
#------------------------------------------------------------------
Include(FetchContent)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.0.1) # or a later release
FetchContent_MakeAvailable(Catch2)

add_executable(realtime_memory_tests
  main.cpp
  resource_tests.cpp;
  allocator_tests.cpp;
  utilities_tests.cpp)
target_link_libraries(realtime_memory_tests PRIVATE
  realtime_memory
  Catch2::Catch2WithMain)

if(MSVC)
    target_compile_options(realtime_memory_tests PRIVATE /W4 /WX)
else()
    target_compile_options(realtime_memory_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
