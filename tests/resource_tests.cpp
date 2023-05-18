//==============================================================================
// Copyright (c) 2019-2022 CradleApps, LLC - All Rights Reserved
//
// This file is part of the Cradle Engine. Unauthorised copying and
// redistribution is strictly prohibited. Proprietary and confidential.
//==============================================================================

#include <unordered_set>
#include <unordered_map>

#include "realtime_memory/memory_resources.h"
#include "realtime_memory/free_list_resource.h"
#include "realtime_memory/containers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_random.hpp>
#include <catch2/generators/catch_generators_range.hpp>

//==============================================================================
/** To track the allocations farmed out to a memory_resource's "upstream". */
class tracking_memory_resource : public std_pmr::memory_resource
{
public:
    void* do_allocate (std::size_t bytes, std::size_t align) override
    {
        // Since malloc gives use max aligned pointers, we don't need to pad
        // for alignments less than that to allow space for alignment.
        auto size = align > alignof(std::max_align_t) ? bytes + align : bytes;
        auto ptr = std::malloc (size);
        auto ptr_front = ptr;
        auto ptr_aligned = std::align (align, bytes, ptr /*mutated*/, size);
        records.push_back (record {ptr_front, ptr_aligned, size});
        return ptr_aligned;
    }

    void do_deallocate (void* ptr, std::size_t /*bytes*/, std::size_t /*align*/) override
    {
        auto match = [ptr] (record& r) { return r.ptr_aligned == ptr; };

        if (auto it = std::find_if (records.begin(), records.end(), match); it != records.end())
        {
            std::free (it->ptr);
            records.erase (it);
            return;
        }

        throw std::logic_error ("Logic error in the test fixture!");
    }

    bool do_is_equal (const memory_resource&) const noexcept override
    {
        return false;
    }

    std::size_t total_allocated() const
    {
        return std::accumulate (records.begin(), records.end(), std::size_t (0),
            [] (std::size_t n, const record& r) { return n + r.num_bytes; });
    }

    std::size_t allocations_count() const
    {
        return records.size();
    }

private:
    struct record
    {
        void* ptr;
        void* ptr_aligned;
        std::size_t num_bytes;
    };

    std::vector<record> records;
};

//==============================================================================
/** A few helpers shared between various tests. */
namespace helpers
{
bool isInsideBuffer (void* ptrToCheck, const std::vector<std::byte>& buffer)
{
    return ptrToCheck >= buffer.data() && ptrToCheck < (buffer.data() + buffer.size());
}

cradle::pmr::string generateRandomString (Catch::Generators::GeneratorWrapper<std::size_t>& rnd, cradle::pmr::memory_resource& r)
{
    cradle::pmr::string s (&r);
    s.reserve (rnd.get());

    for (std::size_t i = 0; i < rnd.get(); ++i)
        s.push_back ('?');

    rnd.next();
    return s;
}

constexpr std::size_t numTestAllocs = 1 << 13;
}

//==============================================================================
TEST_CASE ("get/set default resource")
{
    namespace pmr = cradle::pmr;

    SECTION ("Initial default is new_delete")
    {
        CHECK (pmr::get_default_resource() == pmr::new_delete_resource());
    }

    SECTION ("Can set a different default resource")
    {
        CHECK (pmr::set_default_resource (pmr::null_memory_resource()));
        CHECK (pmr::get_default_resource() == pmr::null_memory_resource());
        CHECK (pmr::set_default_resource (pmr::new_delete_resource())); // restore default
    }
}

//==============================================================================
/** Defined in <stdnext/memory_resource.h> but tested here alongside our custom 'recycling' resource. */
TEST_CASE ("monotonic_buffer_resource", "[memory_resource]")
{
    namespace pmr = cradle::pmr;

    SECTION ("Only compares equal with itself")
    {
        pmr::monotonic_buffer_resource mono1;
        pmr::monotonic_buffer_resource mono2;

        CHECK (mono1 == mono1);
        CHECK_FALSE (mono1 == mono2);
    }

    SECTION ("Non-copyable")
    {
        static_assert (! std::is_copy_constructible_v<pmr::monotonic_buffer_resource>,
                       "monotonic_buffer_resource must not be copy constructible");
        static_assert (! std::is_copy_assignable_v<pmr::monotonic_buffer_resource>,
                       "monotonic_buffer_resource must not be copy assignable");
    }

    SECTION ("Uses default memory_resource as upstream")
    {
        tracking_memory_resource res;
        pmr::set_default_resource (&res);

        pmr::unsynchronized_pool_resource pool;
        CHECK (pool.upstream_resource() == &res);

        pmr::set_default_resource (pmr::new_delete_resource());
    }

    SECTION ("Uses supplied memory_resource as upstream")
    {
        pmr::monotonic_buffer_resource mono (pmr::null_memory_resource());

        CHECK (mono.upstream_resource() == pmr::null_memory_resource());
    }

    SECTION ("Uses initial_size hint")
    {
        tracking_memory_resource upstream;
        pmr::monotonic_buffer_resource mono (3000, &upstream);
        auto p = mono.allocate (1);

        // It may be bigger, as it's allowed to allocate for internal bookkeeping.
        CHECK (upstream.total_allocated() >= 3000);

        mono.deallocate (p, 1); // don't leak, using malloc as a source.
    }

    SECTION ("Uses supplied buffer until full")
    {
        std::vector<std::byte> buffer (2048, std::byte(0));
        pmr::monotonic_buffer_resource mono (buffer.data(), buffer.size());

        CHECK (mono.upstream_resource() == pmr::get_default_resource());

        auto ptr1 = mono.allocate (1024, 1);
        auto ptr2 = mono.allocate (1024, 1);

        CHECK (helpers::isInsideBuffer (ptr1, buffer));
        CHECK (helpers::isInsideBuffer (ptr2, buffer));

        auto ptr3 = mono.allocate (16, 1);
        CHECK_FALSE (helpers::isInsideBuffer (ptr3, buffer));
    }

    SECTION ("Uses supplied buffer, falling back to supplied memory_resource")
    {
        std::vector<std::byte> buffer (2048, std::byte(0));
        pmr::monotonic_buffer_resource mono (buffer.data(), buffer.size(), pmr::null_memory_resource());

        auto ptr1 = mono.allocate (1024, 1);
        auto ptr2 = mono.allocate (1024, 1);

        CHECK (helpers::isInsideBuffer (ptr1, buffer));
        CHECK (helpers::isInsideBuffer (ptr2, buffer));
        CHECK_THROWS_AS (mono.allocate (16), std::bad_alloc);
    }

    SECTION ("Deallocate is a no-op")
    {
        std::vector<std::byte> buffer (2048, std::byte(0));
        pmr::monotonic_buffer_resource mono (buffer.data(), buffer.size());

        auto ptr1 = mono.allocate (1024, 1);
        CHECK (ptr1 == buffer.data());

        mono.deallocate (ptr1, 1024);

        auto ptr2 = mono.allocate (1024, 1);
        CHECK (ptr2 == buffer.data() + 1024);
    }

    SECTION ("Delivers any power of two alignment")
    {
        std::vector<std::byte> buffer (2048, std::byte(0));
        pmr::monotonic_buffer_resource mono (buffer.data(), buffer.size());

        auto ptr1 = mono.allocate (3, 1);
        CHECK (ptr1 == buffer.data());

        auto align = GENERATE (as<std::size_t>(), 1, 2, 4, 8, 16, 32, 64);
        auto ptr2 = mono.allocate (4, align);

        CHECK (std::size_t (ptr2) % align == 0);
    }
}

//==============================================================================
/** Defined in <stdnext/memory_resource.h> but tested here alongside our custom 'recycling' resource. */
TEST_CASE ("unsynchronized_pool_resource", "[memory_resource]")
{
    namespace pmr = cradle::pmr;

    SECTION ("Only compares equal with itself")
    {
        pmr::unsynchronized_pool_resource pool1;
        pmr::unsynchronized_pool_resource pool2;

        CHECK (pool1 == pool1);
        CHECK_FALSE (pool1 == pool2);
    }

    SECTION ("Non-copyable")
    {
        static_assert (! std::is_copy_constructible_v<pmr::unsynchronized_pool_resource>,
                       "unsynchronized_pool_resource must not be copy constructible");
        static_assert (! std::is_copy_assignable_v<pmr::unsynchronized_pool_resource>,
                       "unsynchronized_pool_resource must not be copy assignable");
    }

    SECTION ("Limits pool_options values")
    {
        constexpr auto max_u64 = std::numeric_limits<std::size_t>::max();
        constexpr auto max_u32 = (std::size_t) std::numeric_limits<std::uint32_t>::max();
        const auto maxBlocks = GENERATE (as<size_t>(), 32ul, 1 << 15, max_u64 - 1);
        const auto largestBlock = GENERATE (as<size_t>(), 32ul, 1 << 15, max_u64 - 1);
        CAPTURE (maxBlocks, largestBlock);

        pmr::pool_options options {
            .max_blocks_per_chunk = maxBlocks,
            .largest_required_pool_block = largestBlock
        };
        pmr::unsynchronized_pool_resource pool (options, nullptr);

        const auto expectedLargestBlock = std::min (largestBlock, max_u32);
        const auto expectedMaxBlocks = std::min (maxBlocks, max_u32 / expectedLargestBlock);
        CHECK (pool.options().largest_required_pool_block == expectedLargestBlock);
        CHECK (pool.options().max_blocks_per_chunk == expectedMaxBlocks);
    }

    SECTION ("Uses default memory_resource as upstream")
    {
        tracking_memory_resource res;
        pmr::set_default_resource (&res);

        pmr::unsynchronized_pool_resource pool;
        CHECK (pool.upstream_resource() == &res);

        pmr::set_default_resource (pmr::new_delete_resource());
    }

    SECTION ("Uses supplied memory_resource as upstream")
    {
        pmr::monotonic_buffer_resource upstream;
        pmr::unsynchronized_pool_resource pool (&upstream);

        CHECK (pool.upstream_resource() == &upstream);
    }

    SECTION ("Oversized allocations are serviced by the upstream resource")
    {
        tracking_memory_resource src;
        pmr::unsynchronized_pool_resource pool (&src);

        auto bytes = pool.options().largest_required_pool_block + 1;
        auto ptr1 = pool.allocate (bytes, 8);
        auto numFromUpstream = src.allocations_count();
        auto ptr2 = pool.allocate (bytes, 8);

        CHECK (src.allocations_count() > numFromUpstream);

        pool.deallocate (ptr1, bytes, 8);
        pool.deallocate (ptr2, bytes, 8);
    }

    SECTION ("Overaligned allocations are serviced by the upstream resource")
    {
        tracking_memory_resource src;
        pmr::unsynchronized_pool_resource pool (&src);

        auto align = alignof(std::max_align_t) * GENERATE (as<std::size_t>(), 2, 4, 8);
        auto ptr1 = pool.allocate (3, align);
        auto numFromUpstream = src.allocations_count();
        auto ptr2 = pool.allocate (3, align);

        CHECK (src.allocations_count() > numFromUpstream);

        pool.deallocate (ptr1, 3, align);
        pool.deallocate (ptr2, 3, align);
    }

    SECTION ("Delivers alignment up to alignof(std::max_align_t)")
    {
        tracking_memory_resource src;
        pmr::unsynchronized_pool_resource pool (&src);

        auto max = pool.options().largest_required_pool_block + 7;
        auto bytes = GENERATE_COPY (as<std::size_t>(), 1, 3, 7, 8, 133, max);
        auto align = GENERATE (as<std::size_t>(), 1, 2, 4, 8, alignof(std::max_align_t));
        CAPTURE (bytes, align);

        auto ptr1 = pool.allocate (3, 1);
        auto ptr2 = pool.allocate (bytes, align);

        CHECK (std::size_t (ptr2) % align == 0);

        pool.deallocate (ptr1, 3, 1);
        pool.deallocate (ptr2, bytes, align);
    }
}

//==============================================================================
/** Our custom 'recycling' resource. */
TEST_CASE ("free_list_resource", "[memory_resource]")
{
    std::vector<std::byte> buf1 (256, std::byte(0)), buf2 (256, std::byte(0));

    SECTION ("Only compares equal with itself")
    {
        cradle::pmr::free_list_resource res1 (buf1.data(), buf1.size());
        cradle::pmr::free_list_resource res2 (buf2.data(), buf2.size());

        CHECK (res1 == res1);
        CHECK_FALSE (res1 == res2);
    }

    SECTION ("Non-copyable")
    {
        static_assert (! std::is_copy_constructible_v<cradle::pmr::free_list_resource>,
                       "FreeListBufferResource must not be copy constructible");
        static_assert (! std::is_copy_assignable_v<cradle::pmr::free_list_resource>,
                       "FreeListBufferResource must not be copy assignable");
    }

    SECTION ("Throws bad_alloc when the buffer is exhausted")
    {
        cradle::pmr::free_list_resource res (buf1.data(), buf1.size());

        CHECK_NOTHROW (res.allocate (buf1.size() - 32, 4));
        CHECK_THROWS_AS (res.allocate (32, 4), std::bad_alloc);
    }

    SECTION ("Throws bad_alloc on a request for overaligned memory")
    {
        cradle::pmr::free_list_resource res (buf1.data(), buf1.size());

        auto align = alignof(std::max_align_t) * 2;

        CHECK_THROWS_AS (res.allocate (4, align), std::bad_alloc);
    }

    SECTION ("Delivers alignment up to alignof(std::max_align_t)")
    {
        cradle::pmr::free_list_resource res (buf1.data(), buf1.size());

        auto align = GENERATE (as<std::size_t>(), 1, 2, 4, alignof(std::max_align_t));
        auto bytes = GENERATE (as<std::size_t>(), 1, 3, 7, 8, 41, 77);
        CAPTURE (bytes, align);

        auto ptr1 = res.allocate (3, 1);
        auto ptr2 = res.allocate (bytes, align);

        CHECK (std::size_t (ptr2) % align == 0);

        res.deallocate (ptr1, 3, 1);
        res.deallocate (ptr2, bytes, align);
    }

    SECTION ("Re-merges split blocks that have been returned (defragmentation)")
    {
        std::vector<std::byte> buf (512, std::byte(0));
        std::vector<void*> ptrs;

        cradle::pmr::free_list_resource res (buf.data(), buf.size());

        const std::size_t alignment = 1;
        const std::size_t smallBlockSize = 2;
        const std::size_t largeBlockSize = 200;

        // Allocate small blocks that split up the free list.
        // We don't know the exact capacity of the resource due
        // to its internal bookkeeping, so we keep going until it throws.
        try
        {
            while (true)
                ptrs.push_back (res.allocate (smallBlockSize, alignment));
        }
        catch (const std::bad_alloc&)
        {}

        // Return all the blocks except one in the middle.
        // We do this in random order, to ensure the the free list must be sorted.
        const auto middlePtr = ptrs[ptrs.size() / 2];
        const auto permutation = Catch::Generators::random (0ul, ptrs.size()).next();

        for (std::size_t i = 0; i < permutation; ++i)
            std::next_permutation (ptrs.begin(), ptrs.end());

        for (auto p : ptrs)
            if (p != middlePtr)
                res.deallocate (p, smallBlockSize, alignment);

        // Check that the two areas either side of the still-active pointer
        // can be re-merged in a defragmentation step.
        CHECK (res.allocate (largeBlockSize, alignment));
        CHECK (res.allocate (largeBlockSize, alignment));
        CHECK_THROWS_AS (res.allocate (largeBlockSize, alignment), std::bad_alloc);
    }
}

//==============================================================================
/** Common tests that all three resources should satisfy.
 *  Some of these are just sanity checks that "it doesn't crash".
 */
struct BackingBuffer
{
    static constexpr std::size_t size = helpers::numTestAllocs * 1024;
    std::vector<std::byte> mem {size, std::byte()};
};

struct MonotonicBufferFixture
{
    BackingBuffer buffer;
    cradle::pmr::monotonic_buffer_resource resource {buffer.mem.data(), buffer.size};
};

struct FreeListFixture
{
    BackingBuffer buffer;
    cradle::pmr::free_list_resource resource {buffer.mem.data(), buffer.size};

    std::size_t allocationsCount() const { return resource.num_allocations(); }
    std::size_t allocationsSize() const { return buffer.size - resource.available_space(); }
};

struct UnsyncPoolFixture
{
    FreeListFixture src;
    cradle::pmr::unsynchronized_pool_resource resource {&src.resource};

    std::size_t allocationsCount() const { return src.allocationsCount(); }
    std::size_t allocationsSize() const { return src.allocationsSize(); }
};

TEMPLATE_TEST_CASE ("memory_resource common tests", "[memory_resource]",
                    MonotonicBufferFixture, UnsyncPoolFixture, FreeListFixture)
{
    namespace pmr = cradle::pmr;

    TestType fixture;
    auto& resource = fixture.resource;

    constexpr std::size_t numAllocs = helpers::numTestAllocs;

    SECTION ("Zero sized allocation")
    {
        CHECK_THROWS (resource.allocate (0, 4));
    }

    SECTION ("Single sized allocations")
    {
        std::unordered_set<void*> ptrs;

        constexpr auto alignment = 4;
        const auto numBytes = GENERATE (as<std::size_t>(), 8, 11, 16, 19, 32, 33, 170, 256);

        for (std::size_t i = 0; i < numAllocs; ++i)
            ptrs.insert (resource.allocate (numBytes, alignment));

        SECTION ("Yields unique block addresses")
        {
            CHECK (ptrs.size() == numAllocs);
        }

        SECTION ("Can free blocks at random")
        {
            for (auto it = ptrs.begin(); ptrs.size() > numAllocs / 2; it = ptrs.erase (it))
                resource.deallocate (*it, numBytes, alignment);

            CHECK_NOTHROW (ptrs.insert (resource.allocate (numBytes, alignment)));
        }

        for (auto& ptr : ptrs)
            resource.deallocate (ptr, numBytes, alignment);
    }

    SECTION ("Mixed size allocations")
    {
        auto rnd = Catch::Generators::random<std::size_t> (1, 512);

        std::unordered_map<void*, std::size_t> ptrs;
        constexpr auto alignment = 4;

        for (std::size_t i = 0; i < numAllocs; ++i, rnd.next())
        {
            const auto size = std::size_t (rnd.get());
            const auto ptr = resource.allocate (size, alignment);
            ptrs.insert ({ptr, size});
        }

        SECTION ("Yields unique block addresses")
        {
            CHECK (ptrs.size() == numAllocs);
        }

        SECTION ("Can free blocks at random")
        {
            for (auto it = ptrs.begin(); ptrs.size() > numAllocs / 2; it = ptrs.erase (it))
                resource.deallocate (it->first, it->second);

            CHECK_NOTHROW (resource.allocate (16));
        }

        for (auto& pair : ptrs)
            resource.deallocate (pair.first, pair.second, alignment);
    }

    SECTION ("Nested container allocations")
    {
        pmr::vector<pmr::string> strings (&resource);
        auto rndStrSize = Catch::Generators::random<std::size_t> (sizeof(pmr::string), 50);

        for (std::size_t i = 0; i < 2000; ++i)
            strings.push_back (helpers::generateRandomString (rndStrSize, resource));

        CHECK_NOTHROW (strings.clear());
    }

    SECTION ("Mixed container allocations")
    {
        struct five { char data[5]; };
        struct ninetyNine { char data[99]; };
        struct mixed
        {
            int w = 99;
            char x = 'x';
            std::size_t y = 1;
            long long z = 300;
        };

        pmr::vector<char> chars (&resource);
        pmr::vector<int> ints (&resource);
        pmr::vector<five> fives (&resource);
        pmr::vector<ninetyNine> ninetyNines (&resource);
        pmr::vector<mixed> mixeds (&resource);
        pmr::vector<pmr::string> strings (&resource);

        auto rndVec = Catch::Generators::random<std::size_t> (0, 6);
        auto rndChar = Catch::Generators::random<int> (0, (int) std::numeric_limits<char>::max());
        auto rndInt = Catch::Generators::random<int> (0, std::numeric_limits<int>::max());
        auto rndStrSize = Catch::Generators::random<std::size_t> (sizeof(pmr::string), 50);

        for (int i = 0; i < 2000; ++i)
        {
            switch (rndVec.get())
            {
                case 0: chars.push_back ((char) rndChar.get()); break;
                case 1: ints.push_back (rndInt.get()); break;
                case 2: fives.push_back (five()); break;
                case 3: ninetyNines.push_back (ninetyNine()); break;
                case 4: mixeds.push_back (mixed()); break;
                case 5: strings.push_back (helpers::generateRandomString (rndStrSize, resource)); break;
                default: continue;
            }

            rndVec.next();
            rndChar.next();
            rndInt.next();
        }

        // Touch some random memory to check alignment's correct
        // and nothing crashes when we try to access it.
        for (auto& m : mixeds)
        {
            m.w = rndInt.next(), rndInt.get();
            m.y = (std::size_t) m.w;
            CAPTURE (m.y); // prevent loop from being optimised out
        }

        for (auto& s : strings)
        {
            s[s.size() / 2] = (char) rndChar.get();
            s[s.size() - 1] = (char) rndChar.get();
            CAPTURE (s); // prevent loop from being optimised out
        }

        // Deallocate a lot of blocks
        CHECK_NOTHROW (mixeds.clear());
        CHECK_NOTHROW (strings.clear());

        // A final check that we can allocate more blocks of any size.
        std::vector<void*> ptrs;
        constexpr std::size_t maxSize = 512;

        for (std::size_t i = 1; i < maxSize; ++i)
            CHECK_NOTHROW (ptrs.push_back (resource.allocate (i)));

        for (std::size_t i = 0; i < (maxSize - 1); ++i)
            CHECK_NOTHROW (resource.deallocate (ptrs[i], i + 1));
    }

    SECTION ("Recovery from memory exhaustion")
    {
        CAPTURE (typeid (fixture).name());
        std::vector<void*> ptrs;

        try
        {
            while (true)
                ptrs.emplace_back (resource.allocate (1024));
        }
        catch (const std::bad_alloc&)
        {
            // It's ok for this to throw again, but we don't want it to crash.
            try { ptrs.emplace_back (resource.allocate (1024)); }
            catch (const std::bad_alloc&) {}

            // Check we can deallocate after a bad_alloc throw.
            for (auto ptr : ptrs)
                resource.deallocate (ptr, 1024);

            // Check we can reuse memory after a bad_alloc throw.
            if constexpr (! std::is_same_v<MonotonicBufferFixture, TestType>)
            {
                const auto numToRetry = ptrs.size();
                ptrs.clear();

                CHECK_NOTHROW([&] {
                    for (size_t i = 0; i < numToRetry; ++i)
                        ptrs.push_back(resource.allocate(1024));
                });

                for (auto ptr : ptrs)
                    resource.deallocate (ptr, 1024);
            }
        }
    }
}

//==============================================================================
TEMPLATE_TEST_CASE ("Pool & Free-List memory re-use", "[memory_resource]", UnsyncPoolFixture, FreeListFixture)
{
    namespace pmr = cradle::pmr;

    TestType fixture;
    auto& resource = fixture.resource;

    SECTION ("Can re-use blocks without allocating more space")
    {
        constexpr auto alignment = 4;
        auto rndSize = Catch::Generators::random<std::size_t> (1, 512);

        auto ptrs = std::unordered_map<void*, std::size_t>();
        auto freedBlocks = std::vector<std::size_t>();

        for (std::size_t i = 0; i < helpers::numTestAllocs; ++i, rndSize.next())
        {
            const auto size = std::size_t (rndSize.get());
            const auto ptr = resource.allocate (size, alignment);
            ptrs.insert ({ptr, size});
        }

        const auto initialCount = fixture.allocationsCount();
        const auto initialSize = fixture.allocationsSize();

        for (auto it = ptrs.begin(); ptrs.size() > helpers::numTestAllocs/2; it = ptrs.erase (it))
        {
            freedBlocks.push_back (it->second);
            resource.deallocate (it->first, it->second);
        }

        const auto afterDeallocateCount = fixture.allocationsCount();
        const auto afterDeallocateSize = fixture.allocationsSize();

        // Shuffle so they're not re-allocated in the same order
        for (std::size_t i = 0; i < freedBlocks.size(); ++i, rndSize.next())
            std::swap (freedBlocks[i], freedBlocks[rndSize.get()]);

        // Reallocate the same blocks in a different order
        for (auto& size : freedBlocks)
            ptrs.insert ({resource.allocate (size, alignment), size});

        CHECK (ptrs.size() == helpers::numTestAllocs);
        CHECK (afterDeallocateCount <= initialCount);
        CHECK (afterDeallocateSize <= initialSize);
        CHECK (initialCount <= fixture.allocationsCount());
        CHECK (initialSize <= fixture.allocationsSize());

        for (auto& pair : ptrs)
            resource.deallocate (pair.first, pair.second, alignment);
    }
}

//==============================================================================
/** Special memory overhead characteristics of unsynchronized_pool_resource. */
TEST_CASE ("unsynchronized_pool_resource overhead limits", "[memory_resource]")
{
    namespace pmr = cradle::pmr;

    tracking_memory_resource src;
    pmr::unsynchronized_pool_resource resource {&src};

    SECTION ("Median overhead for book-keeping and pre-allocation is low")
    {
        // We only measure overhead for block sizes greater than fundamental
        // alignment. We can't expect low overhead for sizes less than that.
        const auto minNumBytes = alignof (std::max_align_t);
        const auto numBytes = GENERATE_COPY (as<std::size_t>(), minNumBytes, 19, 32, 33, 170, 256);
        constexpr auto alignment = 4;

        // The overheads vary at different points in time, depending on
        // internal pre-reserved memory, whether the allocations fit
        // snugly into a block etc, so we will take the median overhead,
        // and we allow a larger overhead value for irregular sizes.
        const auto maxMedianOverhead = (numBytes % 8 == 0) ? 2.0 : 3.0;
        auto ptrs = std::unordered_set<void*>();
        auto overheads = std::vector<double>();

        for (std::size_t i = 0; i < helpers::numTestAllocs; ++i)
        {
            ptrs.insert (resource.allocate (numBytes, alignment));
            overheads.push_back (double (src.total_allocated()) / double (numBytes * (i + 1)));
        }

        std::sort (overheads.begin(), overheads.end(), std::less<>());

        const auto min = overheads[0];
        const auto median = overheads[helpers::numTestAllocs/2];
        const auto max = overheads.back();

        CAPTURE (numBytes, min, median, max);
        CHECK (median < maxMedianOverhead);

        for (auto& ptr : ptrs)
            resource.deallocate (ptr, numBytes, alignment);
    }
}
