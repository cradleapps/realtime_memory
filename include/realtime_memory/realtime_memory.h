//==============================================================================
// Copyright (c) 2019-2022 CradleApps, LLC - All Rights Reserved
//==============================================================================
/** A fill-in implementation of C++17 <memory_resource>, to get
 *  monotonic_buffer_resource and unsynchronized_pool_resource.
 *
 *  Sadly these haven't made it to libc++ (clang) yet, though MSVC
 *  and newer GCCs have got implementations. This file is intended
 *  to be a drop-in replacement, and it is inspired by the MSVC
 *  and Bloomberg implementations (permissive licenses).
 *
 *  Example: this is a pool-based allocator that sources its memory
 *  from a pre-allocated buffer, and never calls malloc/new/delete.
 *  This would be suitable for use on a real-time thread, but not from
 *  multiple threads at once - the unsynchronized_pool is not threadsafe.
 *  If the buffer is exhausted, any allocation will throw std::bad_alloc.
 *  myVec is a vector which uses this allocator as a memory source.
 *  myVec2 is the same kind of vector, but uses a more convenient syntax.
 *
 *    using namespace cradle::pmr;
 *    auto b = std::vector<std::byte> (2048, std:byte (0));
 *    auto m = monotonic_buffer_resource (b.data(), b.size(), null_memory_resource());
 *    auto pool = unsynchronized_pool_resource (&m);
 *
 *    auto myVec = std::vector<int, propagating_allocator<int>> (&pool);
 *    auto myVec2 = cradle::pmr::vector<int> (&pool);
 *
 *  Note: if many allocations exceed pool_options::largest_required_pool_block
 *  and they need to be freed later, this set-up will waste a lot of memory.
 *  It's because the pool defers 'oversized' allocations to its upstream source.
 *  In this case that's a monotonic_buffer_resource, which cannot free memory.
 */
#include "memory_resources.h"
#include "polymorphic_allocators.h"
#include "containers.h"
