//==============================================================================
// Copyright (c) 2019-2022 CradleApps, LLC - All Rights Reserved
//==============================================================================
#include <map>
#include <unordered_map>
#include <string>
#include <vector>
#include "polymorphic_allocators.h"

namespace cradle::pmr
{
template <typename T>
using vector = std::vector<T, propagating_allocator<T>>;

template <typename T = char>
using basic_string = std::basic_string<T, std::char_traits<T>, propagating_allocator<T>>;
using string = basic_string<char>;

template <typename Key, typename Value>
using map = std::map<Key, Value, propagating_allocator<Value>>;

template <typename Key, typename Value>
using unordered_map = std::unordered_map<Key, Value, propagating_allocator<Value>>;
}
