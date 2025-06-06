#include <algorithm>
#include <atomic>
#include <cassert>
#include <vector>

#if defined (__apple_build_version__)
  #if (__apple_build_version__ < 1500000)
    #define USE_EXPERIMENTAL_PMR 1
  #elif defined (__MAC_OS_X_VERSION_MIN_REQUIRED) && (__MAC_OS_X_VERSION_MIN_REQUIRED < 101400)
    #define USE_EXPERIMENTAL_PMR 1
  #else
    #define USE_EXPERIMENTAL_PMR 0
  #endif
#elif defined (__clang__) && (_LIBCPP_VERSION < 1600)
  #define USE_EXPERIMENTAL_PMR 1
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

#if defined (__apple_build_version__) && USE_EXPERIMENTAL_PMR
  #define PMR_DIAGNOSTIC_PUSH \
     _Pragma("clang diagnostic push") \
      _Pragma("clang diagnostic ignored \"-Wdeprecated-declarations\"")
  #define PMR_DIAGNOSTIC_POP \
     _Pragma("clang diagnostic pop")
#else
  #define PMR_DIAGNOSTIC_PUSH
  #define PMR_DIAGNOSTIC_POP
#endif
