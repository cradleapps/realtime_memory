//==============================================================================
// Copyright (c) 2019-2023 CradleApps, LLC - All Rights Reserved
//==============================================================================

#include <catch2/catch_test_macros.hpp>
#include "realtime_memory/memory_resources.h"
#include "realtime_memory/polymorphic_allocators.h"
#include "realtime_memory/containers.h"

TEST_CASE ("Allocator propagated with string copy construction", "[RealtimeMemory]")
{
    cradle::pmr::monotonic_buffer_resource resource (8192);
    cradle::pmr::propagating_allocator<std::byte> allocator (&resource);
    cradle::pmr::string s1 (allocator);

    cradle::pmr::string s2 (s1);

    CHECK (s2.get_allocator() == allocator);
}

TEST_CASE ("Allocator moved with string move construction", "[RealtimeMemory]")
{
    cradle::pmr::monotonic_buffer_resource resource (8192);
    cradle::pmr::propagating_allocator<std::byte> allocator (&resource);
    cradle::pmr::string s1 (allocator);

    cradle::pmr::string s2 (std::move (s1));

    CHECK (s2.get_allocator() == allocator);
}

 TEST_CASE ("Allocator propagated with string assignment", "[RealtimeMemory]")
{
    cradle::pmr::monotonic_buffer_resource resource (8192);
    cradle::pmr::propagating_allocator<std::byte> allocator (&resource);
    cradle::pmr::string s1 ("uses allocator", allocator);
    cradle::pmr::string s2 ("system allocator", cradle::pmr::propagating_allocator<std::byte> (cradle::pmr::new_delete_resource()));

    CAPTURE (s2); // prevent optimisation away
    REQUIRE (s2.get_allocator() != allocator);

    s2 = s1;

    CHECK (s2.get_allocator() == allocator);
}

TEST_CASE ("Allocator propagated with string move assignment", "[RealtimeMemory]")
{
    cradle::pmr::monotonic_buffer_resource resource (8192);
    cradle::pmr::propagating_allocator<std::byte> allocator (&resource);
    cradle::pmr::string s1 ("uses allocator", allocator);
    cradle::pmr::string s2 ("system allocator", cradle::pmr::propagating_allocator<std::byte> (cradle::pmr::new_delete_resource()));

    CAPTURE (s2); // prevent optimisation away
    REQUIRE (s2.get_allocator() != allocator);

    s2 = std::move (s1);

    CHECK (s2.get_allocator() == allocator);
}

TEST_CASE ("Allocator propagated with vector copy construction", "[RealtimeMemory]")
{
    cradle::pmr::monotonic_buffer_resource resource (8192);
    cradle::pmr::propagating_allocator<std::byte> allocator (&resource);
    cradle::pmr::vector<int> v1 (allocator);

    cradle::pmr::vector<int> v2 (v1);

    CHECK (v2.get_allocator() == allocator);
}

TEST_CASE ("Allocator propagated with nested container copy construction", "[RealtimeMemory]")
{
    cradle::pmr::monotonic_buffer_resource resource (8192);
    cradle::pmr::propagating_allocator<std::byte> allocator (&resource);
    cradle::pmr::vector<cradle::pmr::string> v1 (allocator);
    v1.emplace_back ("hello", allocator);
    REQUIRE (v1.size() == 1);
    REQUIRE (v1[0].get_allocator() == allocator);

    cradle::pmr::vector<cradle::pmr::string> v2 (v1);

    REQUIRE (v2.size() == 1);
    CHECK (v2[0].get_allocator() == allocator);
}
