#include <algorithm>
#include <atomic>
#include <cassert>
#include <vector>

#ifdef __clang__
  #ifdef __apple_build_version__
    #define USE_EXPERIMENTAL_PMR _LIBCPP_VERSION < 1500
  #else
    #define USE_EXPERIMENTAL_PMR _LIBCPP_VERSION < 1600
  #endif
#else
  #define USE_EXPERIMENTAL_PMR 0
#endif


#if USE_EXPERIMENTAL_PMR
  #include <experimental/memory_resource>
  namespace std_pmr = std::experimental::pmr;
#else
  #include <memory_resource>
  namespace std_pmr = std::pmr;
#endif
