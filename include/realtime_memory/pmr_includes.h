#include <algorithm>
#include <atomic>
#include <cassert>
#include <stdexcept>
#include <utility>
#include <vector>

#if defined (__apple_build_version__)
 #define USE_PMR_SHIM 1
#elif defined (__clang__)
 #define USE_PMR_SHIM _LIBCPP_VERSION < 1600
#else
 #define USE_PMR_SHIM 0
#endif

#if USE_PMR_SHIM
 #include "pmr_shim.h"
 namespace std_pmr = rtm_pmr;
#else
 #include <memory_resource>
 namespace std_pmr = std::pmr;
#endif
