//==============================================================================
// Copyright (c) 2019-2022 CradleApps, LLC - All Rights Reserved
//==============================================================================
/** A std::pmr compatible memory_resource that can allocate and
 *  free blocks of any size from a fixed buffer. If the buffer
 *  is exhausted, std::bad_alloc() is thrown.
 *
 *  Important notes:
 *    - This resource is single-threaded
 *    - This resource does not support over-alignment.
 *
 *  If we need these, we may add them, but for now YAGNI.
 *
 *  It's a first-fit free-list algorithm, which is relatively
 *  slow. Thus it's best used for large but rare allocations.
 *
 *  A good technique is to use it as the upstream resource for
 *  an unsynchronized_pool_resource. Small allocations will be
 *  served by the pool (which is fast) while oversize
 *  allocations (which are rarer) will be served by this object.
 *  This object would also provide the memory for the pool itself.
 */
#pragma once
#include <realtime_memory/memory_resources.h>
#include <realtime_memory/utilities.h>

namespace cradle::pmr
{
//==============================================================================
class free_list_resource : public cradle::pmr::identity_equal_resource
{
public:
    free_list_resource (void* buffer, std::size_t buffer_size)
      : space (buffer_size)
    {
        if (buffer == nullptr || buffer_size < sizeof(mem_block))
            throw std::bad_alloc();

        auto pos = align_block (buffer, space);
        first_free = ::new (pos) mem_block (nullptr, space - mem_block::header_size());
    }

    /** Returns the remaining space in the buffer, in bytes. */
    std::size_t available_space() const { return space; }

    /** Returns the number of individual allocations that have been made. */
    std::size_t num_allocations() const { return num_allocs; }

    //==============================================================================
    /** You will interact with the resource via the public functions of its base-class. */
private:
    struct mem_block; // forward-declare.

    void* do_allocate (std::size_t bytes, std::size_t align) override
    {
        if (bytes == 0)
            throw std::bad_alloc();
        if (align > max_align_bytes)
            throw std::bad_alloc(); // Over-alignment is not currently supported!

        const auto size = cradle::pmr::detail::aligned_ceil (bytes, std::min (align, max_align_bytes));

        if ((size + mem_block::header_size()) >= space)
            throw std::bad_alloc(); // out of memory.

        for (mem_block* free = first_free, *prev = nullptr; free != nullptr; prev = free, free = free->next)
            if (free->size >= size)
                return assign_block (*free, prev, size);

        // Too fragmented: we have enough space but no blocks are large enough.
        if (auto defragmented_block = defragment (size))
            return defragmented_block;

        throw std::bad_alloc(); // still too fragmented, abort
    }

    void do_deallocate (void* ptr, std::size_t, std::size_t) override
    {
        const auto last_first_free = std::exchange (first_free, mem_block::get_header (ptr));
        first_free->next = last_first_free;
        space += (mem_block::header_size() + first_free->size);
        --num_allocs;
    }

    void* assign_block (mem_block& block, mem_block* previous_in_free_list, std::size_t required_size)
    {
        // _only split the block if we're going to save a significant number of bytes.
        if ((block.size - required_size) > (mem_block::header_size() + max_align_bytes * 2))
            remove_from_free_list (block, previous_in_free_list, split_block (block, required_size));
        else
            remove_from_free_list (block, previous_in_free_list, block.next);

        block.next = nullptr;
        space -= (mem_block::header_size() + block.size);
        ++num_allocs;
        return block.data();
    }

    static mem_block* split_block (mem_block& block, std::size_t required_size)
    {
        // The block is not large enough to be split into a new block with the required size.
        assert (block.size - required_size > mem_block::header_size());

        auto old_block_data = block.data();
        auto new_block_pos = (void*) (old_block_data + required_size);
        auto new_block_size = (block.size - required_size) - mem_block::header_size();
        auto new_block_pos_aligned = align_block (new_block_pos, new_block_size);
        auto new_block = ::new (new_block_pos_aligned) mem_block (block.next, new_block_size);

        block.size = std::size_t ((std::byte*) new_block_pos_aligned - old_block_data);
        return new_block;
    }

    void remove_from_free_list (mem_block& block_being_removed, mem_block* previous_in_list, mem_block* next_in_list)
    {
        if (&block_being_removed == first_free)
            first_free = next_in_list;
        else if (previous_in_list != nullptr)
            previous_in_list->next = next_in_list;
    }

    // Merges contiguous free blocks, and returns the block that's
    // closest in size to the the desired block size.
    void* defragment (std::size_t desired_block_size)
    {
        // First we sort the free list so that it's ordered by pointer position (lowest first)
        first_free = merge_sort_list (first_free, [] (mem_block* a, mem_block* b) {
            return reinterpret_cast<std::size_t> (a) < reinterpret_cast<std::size_t> (b);
        });

        // Now we iterate the free blocks, merging the next block along if it's also free.
        // While we do this, we look for a block that is suitable for use by the caller.
        std::size_t closest_size_diff = std::numeric_limits<std::size_t>::max();
        mem_block* closest_sized_block = nullptr;
        mem_block* closest_sized_block_prev = nullptr;

        for (mem_block* free = first_free, *prev = nullptr; free != nullptr && free->next != nullptr;)
        {
            auto next = free->next;
            const auto thisStart = free->data();
            const auto thisEnd = thisStart + free->size;
            const auto nextHeader = reinterpret_cast<std::byte*> (next);

            if (nextHeader - thisEnd <= std::ptrdiff_t (max_align_bytes))
            {
                const auto nextEnd = next->data() + next->size;

                free->size = std::size_t (nextEnd - thisStart);
                free->next = next->next;
            }
            else
            {
                if (free->size >= desired_block_size)
                {
                    const auto diff = free->size - desired_block_size;

                    if (diff < closest_size_diff)
                    {
                        closest_size_diff = diff;
                        closest_sized_block = free;
                        closest_sized_block_prev = prev;
                    }
                }

                prev = free;
                free = free->next;
            }
        }

        if (closest_sized_block == nullptr)
            return nullptr;

        return assign_block (*closest_sized_block, closest_sized_block_prev, desired_block_size);
    }

    //==============================================================================
    struct mem_block
    {
        mem_block (mem_block* nx, std::size_t sz)
          : next (nx), size (sz) {}

        mem_block* next = nullptr;
        std::size_t size = 0;

        std::byte* data()
        {
            return (std::byte*) this + header_size();
        }

        static mem_block* get_header (void* ptr)
        {
            return reinterpret_cast<mem_block*> ((std::byte*) ptr - header_size());
        }

        static constexpr std::size_t header_size()
        {
            return cradle::pmr::detail::aligned_ceil (sizeof (mem_block), max_align_bytes);
        }
    };

    // Works like std::align - the input parameters are mutated, and the aligned pointer
    // is also returned. If the alignment can't be obtained, the input parameters are
    // restored to their originals, and nullptr is returned (that shouldn't ever happen).
    static void* align_block (void*& buffer, std::size_t& space)
    {
        static constexpr auto base_align = alignof (mem_block);
        static_assert (mem_block::header_size() % base_align == 0); // sanity check

        auto* ptr = (std::byte*) buffer;
        const auto* end = ptr + space;

        if (auto offset = std::size_t (ptr) & (base_align - 1); offset != 0)
            ptr += base_align - offset;

        while (! is_byte_aligned<max_align_bytes> (ptr + mem_block::header_size()))
            ptr += base_align;

        if (ptr >= end)
            throw std::logic_error ("free_list_resource failed to align block");

        space -= std::size_t (ptr - (std::byte*) buffer);
        buffer = ptr;
        return ptr;
    }

    template <std::size_t byte_alignment>
    static bool is_byte_aligned (const void* address) noexcept
    {
        static_assert (byte_alignment > 0);

        const auto p = reinterpret_cast<std::uintptr_t> (address);
        return p % byte_alignment == 0;
    }

    static constexpr auto max_align_bytes = alignof (std::max_align_t);

    //==============================================================================
    mem_block* first_free = nullptr;
    std::size_t space = 0;
    std::size_t num_allocs = 0;
};

} // namespace cradle
