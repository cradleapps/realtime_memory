#include <algorithm>
#include <atomic>
#include <cassert>
#include <vector>

#if defined(__clang__)
 #include <experimental/memory_resource>
 namespace std_pmr = std::experimental::pmr;
#else
 #include <memory_resource>
 namespace std_pmr = std::pmr;
#endif
