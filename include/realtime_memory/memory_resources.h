//==============================================================================
// Copyright (c) 2019-2022 CradleApps, LLC - All Rights Reserved
//==============================================================================
#pragma once
#include "pmr_includes.h"

PMR_DIAGNOSTIC_PUSH

//==============================================================================
namespace cradle::pmr
{
/** Glue so we can easily switch std::pmr with std::pmr in future. */
using memory_resource = std_pmr::memory_resource;

/** Reimplemented because their definition is missing on ARM! */
memory_resource* get_default_resource() noexcept;
memory_resource* set_default_resource (memory_resource* r) noexcept;
memory_resource* null_memory_resource() noexcept;
memory_resource* new_delete_resource() noexcept;

//==============================================================================
/** The resource types defined in this file. */
class identity_equal_resource : public memory_resource
{
public:
    bool do_is_equal (const memory_resource& x) const noexcept override { return this == &x; }

    identity_equal_resource() = default;
    identity_equal_resource (const identity_equal_resource&) = delete;
    identity_equal_resource& operator= (const identity_equal_resource&) = delete;
};

class null_resource : public identity_equal_resource
{
    void* do_allocate (size_t, size_t) override { throw std::bad_alloc(); }
    void do_deallocate (void*, size_t, size_t) override {}
};

class aligned_new_delete_resource : public identity_equal_resource
{
    void* do_allocate (size_t bytes, size_t align) override { return ::operator new (bytes, std::align_val_t (align)); }
    void do_deallocate (void* ptr, size_t bytes, size_t align) override { ::operator delete (ptr, bytes, std::align_val_t (align)); }
};

class monotonic_buffer_resource; // forward declare
class unsynchronized_pool_resource; // forward declare
struct pool_options; // forward declare

//==============================================================================
/** Implementation details.
 *
 *  Notes on terminology:
 *   - a block is a single slot of memory suitable for an object (e.g. 8 bytes for a uint64_t)
 *   - a chunk is a contiguous region of memory
 *   - a pool is a group of chunks which contain uniformly-sized blocks
 */
namespace detail
{
    /** Round n up to a multiple of alignment, which must be a power of two. */
    constexpr std::size_t aligned_ceil (size_t size, size_t align) noexcept;
    constexpr auto max_alignment = alignof(std::max_align_t);

    /** A list of variable-sized memory chunks, sourced from a memory_resource. */
    struct chunk_list
    {
        void* allocate (std::size_t num_bytes, std::size_t align, memory_resource& upstream);
        void release (memory_resource& upstream);
        void deallocate_chunk (void* address, memory_resource& upstream);

    private:
        struct chunk
        {
            chunk* next = nullptr;
            chunk* prev = nullptr;
            std::byte* ptr = nullptr;
            std::uint32_t size = 0;
            std::uint32_t align = 0;
        };

        chunk* first = nullptr;
        constexpr std::size_t header_size();
    };

    /** An expandable pool that can distribute memory blocks of a uniform size. */
    struct pool
    {
        using block_size_t = std::uint32_t;

        pool (memory_resource&,
              block_size_t block_size_in_bytes); // throws if < min_block_size

        void* allocate (const pool_options&);
        void deallocate (void* block);
        void release();

        block_size_t get_block_size() const { return block_size; }

    private:
        struct chunk
        {
            explicit chunk (block_size_t n, std::byte* b)
                : block_buffer (b), num_blocks (n) {}

            std::byte* block_buffer = nullptr;
            block_size_t num_blocks = 0;
            block_size_t num_initialized = 0;

            inline std::byte* address (block_size_t index, block_size_t block_size) const;
        };

        struct free_link
        {
            explicit free_link (free_link* nxt) : next (nxt) {}
            free_link* next = nullptr;
        };

        chunk& find_or_alloc_chunk_with_space (const pool_options&);

        std::vector<chunk, std_pmr::polymorphic_allocator<chunk>> chunks;
        free_link* next_free = nullptr;
        block_size_t block_size;

    public:
        static constexpr auto min_block_size = block_size_t (std::max (sizeof(free_link), max_alignment));
    };
}

//==============================================================================
/** A memory_resource implementation that allocates from a buffer
 *  and doesn't really deallocate the memory until it is destructed.
 *  This concept is sometimes known as a linear (or bump) allocator.
 *  This class can be constructed with or without an initial buffer
 *  and will always have an "upstream" memory_resource instance from
 *  which instances will request additional memory when the current
 *  buffer is exhausted.
 *
 *   - Deallocation is a no-op. Memory is not reused, but is freed on destruct.
 *   - Allocation is not thread-safe but is very fast while space remains
 *     in the buffer and the upstream resource is not used.
 *   - Individual allocations may have any size up to the provided
 *     buffer's size while it has space. After this, allocations
 *     must be limited to 4GB in size.
 */
class monotonic_buffer_resource : public identity_equal_resource
{
public:
    /** Create a monotonic_buffer_resource with no initial buffer, that will
     *  use the result of pmr::get_default_resource() as its upstream
     *  memory_resource.
     */
    monotonic_buffer_resource() noexcept;

    /** Creates a monotonic_buffer_resource that will used the supplied
     *  buffer as its initial buffer. Upon exhaustion of this initial
     *  buffer, further memory will be allocated from the result of calling
     *  pmr::get_upstream_resource()
     */
    monotonic_buffer_resource (void* buffer, std::size_t buffer_size) noexcept;

    /** Creates a monotonic_buffer_resouce that will use the supplied
     *  buffer as its initial buffer and will allocate from the supplied
     *  upstream resource upon exhaustion of the initial buffer.
     */
    monotonic_buffer_resource (void* buffer,
                               std::size_t buffer_size,
                               memory_resource* upstream) noexcept;

    /** Creates a monotonic_buffer_resource that uses the supplied
     * memory_resource as its upstream resource.
     */
    explicit monotonic_buffer_resource (memory_resource* upstream) noexcept;

    /** Creates a monotonic_buffer_resource that will use the larger of an
     *  internal initial buffer size and the buffer size specified here
     *  as the size of the initial buffer requested from the upstream
     *  resource. In this case since upstream is unspecified, upstream will
     *  be the result of calling pmr::get_default_resource()
     */
    explicit monotonic_buffer_resource (std::size_t initial_size) noexcept;

    /** Creates a monotonic_buffer_resource that will use the larger of
     *  an internal initial buffer size and the buffer size specified here
     *  as the size of the initial buffer requested from the provided
     *  upstream memory_resource
     */
    monotonic_buffer_resource (std::size_t initial_size,
                               memory_resource* upstream) noexcept;

    /** Calls release() */
    ~monotonic_buffer_resource() override;

    monotonic_buffer_resource& operator= (const monotonic_buffer_resource&) = delete;
    monotonic_buffer_resource (const monotonic_buffer_resource&) = delete;

    /** Deallocate all blocks of memory allocated from upstream */
    void release();

    /** Get this instance's upstream memory_resource */
    memory_resource* upstream_resource() const;

private:
    void* do_allocate(std::size_t bytes, std::size_t align) override;
    void do_deallocate(void*, std::size_t bytes, std::size_t align) override;
    void recalculate_next_buffer_size();

    memory_resource& upstream;
    void* currentbuf;
    std::size_t currentbuf_size;
    std::size_t nextbuf_size;
    detail::chunk_list chunks;
};

//==============================================================================
/** Configuration options for a pool_resource.
 *  - largest_required_pool_block will clamped to max_u32.
 *  - max_blocks_per_chunk will be constrained such that no pool
 *    can exceed max_u32 bytes in size.
 */
struct pool_options
{
    std::size_t max_blocks_per_chunk;
    std::size_t largest_required_pool_block;
};

//==============================================================================
/** A memory_resource backed by a series of memory pools, structured
 *  by size. This is a very fast way to allocate heterogeneous memory
 *  regions, but it's not always perfectly economical with memory.
 *  For one thing, a request of e.g. 17 bytes will be placed into a
 *  block of 32 bytes in size. A case that results in even greater
 *  wastage is when a large number of blocks of the same size are
 *  allocated, then deallocated, and then no more blocks of that
 *  size are required again. The area that was used for those
 *  blocks will not be reclaimed until release() is called to reset
 *  the whole resource.
 *
 *  - This class is not threadsafe.
 *  - Allocations larger than pool_options.largest_required_pool_block
 *    are serviced by the upstream resource.
 *  - Alignment must be a power of two, just like system malloc.
 *  - Allocations for overaligned (> alignof(std::max_align_t)) memory
 *    are serviced by the upstream pool.
 *  - Allocations for any other alignment are always aligned to
 *    alignof(std::max_align_t).
 *  - Individual allocations may not exceed 2^32 bytes in size.
 */
class unsynchronized_pool_resource : public identity_equal_resource
{
public:
    /** Instantiate using a default-constructed pool_options and
     *  the memory_resource returned by pmr::get_default_resource()
     */
    unsynchronized_pool_resource();

    /** Instantiate using a default-constructed pool_options and the
     *  provided memory_resource
     */
    explicit unsynchronized_pool_resource (memory_resource* upstream);

    /** Instantiate  using the supplied pool_options and memory_resource */
    explicit unsynchronized_pool_resource (const pool_options& opts,
                                           memory_resource* upstream);

    /** Calls release() */
    ~unsynchronized_pool_resource() override;

    unsynchronized_pool_resource& operator= (const unsynchronized_pool_resource&) = delete;
    unsynchronized_pool_resource (const unsynchronized_pool_resource&) = delete;

    /** Frees all memory allocated via this object, even if that memory
     *  has not been deallocated.
     */
    void release();

    /**  Access the upstream memory resource used by this instance. */
    memory_resource* upstream_resource() const;

    /** Access the pool_options used by this instance. Note that while the
     *  pool_options instance passed to the constructor is a hint, the
     *  value returned from this function reflects the actual pool sizing
     *  values chosen by the implementation.
     */
    pool_options options() const;

protected:
    void* do_allocate (std::size_t bytes, std::size_t align) override;
    void do_deallocate (void* ptr, std::size_t bytes, std::size_t align) override;
    void init_pools();

private:
    detail::pool& which_pool (std::size_t bytes);
    bool is_oversized (std::size_t bytes, std::size_t align) const;

    pool_options opts;
    memory_resource& upstream;
    std::vector<detail::pool, std_pmr::polymorphic_allocator<detail::pool>> pools;
    detail::chunk_list oversized;
};


//============================================================================
// ************************** IMPLEMENTATION *********************************
//============================================================================
inline cradle::pmr::aligned_new_delete_resource default_new_delete_resource;
inline cradle::pmr::null_resource default_null_resource;
inline std::atomic<memory_resource*> default_resource = &default_new_delete_resource;

inline memory_resource* get_default_resource() noexcept { return default_resource.load(); }
inline memory_resource* set_default_resource (memory_resource* r) noexcept { return default_resource.exchange (r); }
inline memory_resource* null_memory_resource() noexcept { return &default_null_resource; }
inline memory_resource* new_delete_resource() noexcept { return &default_new_delete_resource; }

//==============================================================================
namespace detail
{
    inline std::size_t mono_default_nextbuf_size = 32 * sizeof(void*);
    inline std::size_t max_u32 = (std::size_t) std::numeric_limits<std::uint32_t>::max();

    inline pool_options default_pool_options = {
        .max_blocks_per_chunk = std::size_t (1 << 15),
        .largest_required_pool_block = 4096
    };

    // Note: Undefined behaviour if 'align' is not a power of two!
    constexpr std::size_t aligned_ceil (size_t size, size_t align) noexcept
    {
        return (size + align - 1) & ~(align - 1);
    }
}

//==============================================================================
inline void* detail::chunk_list::allocate (std::size_t num_bytes, std::size_t align, memory_resource& upstream)
{
    if (num_bytes == 0 || num_bytes >= max_u32)
        throw std::bad_alloc();

    num_bytes += header_size();
    align = std::max (align, alignof(chunk));

    auto* ptr = upstream.allocate (num_bytes, align);

    if (auto* next = std::exchange (first, ::new (ptr) chunk()))
    {
        first->next = next;
        next->prev = first;
    }

    first->size = std::uint32_t (num_bytes);
    first->align = std::uint32_t (align);
    return (std::byte*) ptr + header_size();
}

inline void detail::chunk_list::release (memory_resource& upstream)
{
    while (first != nullptr)
    {
        auto next = first->next;
        upstream.deallocate (first, first->size, first->align);
        first = next;
    }
}

inline void detail::chunk_list::deallocate_chunk (void* address, memory_resource& upstream)
{
    auto* c = reinterpret_cast<chunk*> ((std::byte*) address - header_size());

    if (c->prev != nullptr)
        c->prev->next = c->next;
    if (c->next != nullptr)
        c->next->prev = c->prev;
    if (c == first)
        first = c->next;

    upstream.deallocate (c, c->size, c->align);
}

inline constexpr std::size_t detail::chunk_list::header_size()
{
    constexpr auto size = aligned_ceil (sizeof(chunk), max_alignment);
    static_assert(size <= sizeof(std::size_t) * 4, "Let's keep the chunk_list header small!");
    return size;
}

//==============================================================================
inline monotonic_buffer_resource::monotonic_buffer_resource() noexcept
    : monotonic_buffer_resource (detail::mono_default_nextbuf_size, nullptr)
{}

inline monotonic_buffer_resource::monotonic_buffer_resource (memory_resource* up) noexcept
    : monotonic_buffer_resource (detail::mono_default_nextbuf_size, up)
{}

inline monotonic_buffer_resource::monotonic_buffer_resource (std::size_t initial_size) noexcept
    : monotonic_buffer_resource (initial_size, nullptr)
{}

inline monotonic_buffer_resource::monotonic_buffer_resource (std::size_t initial_size,
                                                      memory_resource* up) noexcept
    : upstream (up ? *up : *get_default_resource()),
      currentbuf (nullptr),
      currentbuf_size (0),
      nextbuf_size (std::max (initial_size, detail::mono_default_nextbuf_size))
{}

inline monotonic_buffer_resource::monotonic_buffer_resource (void* buf, std::size_t bufsize) noexcept
    : monotonic_buffer_resource (buf, bufsize, nullptr)
{}


inline monotonic_buffer_resource::monotonic_buffer_resource (void* buf,
                                                      std::size_t bufsize,
                                                      memory_resource* up) noexcept
    : upstream (up ? *up : *get_default_resource()),
      currentbuf (buf),
      currentbuf_size (bufsize),
      nextbuf_size (std::max (bufsize, detail::mono_default_nextbuf_size))
{
    recalculate_next_buffer_size();
}

inline monotonic_buffer_resource::~monotonic_buffer_resource()
{
    release();
}


inline void monotonic_buffer_resource::release()
{
    chunks.release (upstream);
}


inline memory_resource* monotonic_buffer_resource::upstream_resource() const
{
    return &upstream;
}


inline void* monotonic_buffer_resource::do_allocate (std::size_t bytes, std::size_t align)
{
    if (bytes == 0)
        throw std::bad_alloc();

    void* allocated = std::align (align, bytes, currentbuf, currentbuf_size);

    if (allocated == nullptr)
    {
        nextbuf_size = std::max (nextbuf_size, detail::aligned_ceil (bytes, align));
        currentbuf = chunks.allocate (nextbuf_size, align, upstream);
        currentbuf_size = nextbuf_size;
        recalculate_next_buffer_size();
        allocated = std::align (align, bytes, currentbuf, currentbuf_size);

        if (allocated == nullptr)
            throw std::bad_alloc();
    }

    currentbuf = reinterpret_cast<std::byte*> (currentbuf) + bytes;
    currentbuf_size -= bytes;
    return allocated;
}


inline void monotonic_buffer_resource::recalculate_next_buffer_size()
{
    nextbuf_size = (detail::max_u32/2 < nextbuf_size) ? detail::max_u32 : currentbuf_size * 2;
}


inline void monotonic_buffer_resource::do_deallocate (void*, std::size_t, std::size_t)
{
    // Do nothing: this resource doesn't deallocate.
}

//==============================================================================
inline detail::pool::pool (memory_resource& r, block_size_t block_size_in_bytes)
    : chunks (&r),
      block_size (block_size_in_bytes)
{
    // invalid block_size given to memory pool
    assert (block_size >= min_block_size);
}

inline void* detail::pool::allocate (const pool_options& opts)
{
    if (next_free != nullptr)
        return std::exchange (next_free, next_free->next);

    auto& chunk = find_or_alloc_chunk_with_space (opts);
    return chunk.address (chunk.num_initialized++, block_size);
}

inline void detail::pool::deallocate (void* block)
{
    next_free = ::new (block) free_link (next_free);
}

inline auto detail::pool::find_or_alloc_chunk_with_space (const pool_options& opts) -> chunk&
{
    if (! chunks.empty())
        if (auto& last = chunks.back(); last.num_initialized < last.num_blocks)
            return last;

    if (chunks.capacity() == 0)
        chunks.reserve (8);

    const auto max_num_blocks = block_size_t (opts.max_blocks_per_chunk);
    const auto min_num_blocks = std::clamp (block_size_t (opts.largest_required_pool_block) / (block_size/2), 2u, 512u);

    auto num_blocks = std::min (max_num_blocks, chunks.empty() ? min_num_blocks : chunks.back().num_blocks * 2);
    auto& resource = *chunks.get_allocator().resource();
    auto* buffer = (std::byte*) resource.allocate (block_size * num_blocks, max_alignment);
    return chunks.emplace_back (num_blocks, buffer);
}

inline void detail::pool::release()
{
    auto& resource = *chunks.get_allocator().resource();

    for (auto& chunk : chunks)
        resource.deallocate (chunk.block_buffer, chunk.num_blocks * block_size, max_alignment);

    chunks.clear();
}

inline std::byte* detail::pool::chunk::address (block_size_t index, block_size_t block_size_) const
{
    return block_buffer + (index * block_size_);
}

//==============================================================================
inline unsynchronized_pool_resource::unsynchronized_pool_resource()
    : unsynchronized_pool_resource (nullptr)
{}

inline unsynchronized_pool_resource::unsynchronized_pool_resource (memory_resource* resource)
    : unsynchronized_pool_resource (detail::default_pool_options, resource)
{}

inline unsynchronized_pool_resource::unsynchronized_pool_resource (const pool_options& opts_,
                                                                   memory_resource* resource)

    : opts (opts_),
      upstream (resource ? *resource : *get_default_resource()),
      pools (&upstream)
{
    opts.largest_required_pool_block = std::min (opts.largest_required_pool_block, detail::max_u32);
    opts.max_blocks_per_chunk = std::min (opts.max_blocks_per_chunk, detail::max_u32 / opts.largest_required_pool_block);
}

inline unsynchronized_pool_resource::~unsynchronized_pool_resource()
{
    release();
}

inline void unsynchronized_pool_resource::release()
{
    for (auto& pool : pools)
        pool.release();

    oversized.release (upstream);
}

inline memory_resource* unsynchronized_pool_resource::upstream_resource() const
{
    return &upstream;
}

inline pool_options unsynchronized_pool_resource::options() const
{
    return opts;
}

inline void unsynchronized_pool_resource::init_pools()
{
    using block_size_t = detail::pool::block_size_t;
    assert (opts.largest_required_pool_block <= detail::max_u32);
    assert (opts.max_blocks_per_chunk <= detail::max_u32);

    const auto largest_block = (block_size_t) opts.largest_required_pool_block;
    auto num_pools = block_size_t (0);

    for (block_size_t n = detail::pool::min_block_size; n <= largest_block; n *= 2)
        ++num_pools;

    pools.reserve (num_pools); // avoid reallocations while building vector

    for (block_size_t n = detail::pool::min_block_size; n <= largest_block; n *= 2)
        pools.emplace_back (upstream, n);
}

inline detail::pool& unsynchronized_pool_resource::which_pool (std::size_t bytes)
{
    for (auto& pool : pools)
        if (pool.get_block_size() >= bytes)
            return pool;

    throw std::logic_error ("unsynchronized_pool_resource can't find appropriate pool");
}

inline void* unsynchronized_pool_resource::do_allocate (std::size_t bytes, std::size_t align)
{
    if (bytes == 0 || bytes > detail::max_u32)
        throw std::bad_alloc();

    if (is_oversized (bytes, align))
        return oversized.allocate (bytes, align, upstream);

    // If we don't do this lazily, testing that pool_options is constrained
    // becomes hard, because the constructor will allocate vast regions of
    // memory when we use big values. Keep an eye on this branch's cost though.
    if (pools.empty())
        init_pools();

    return which_pool (bytes).allocate (opts);
}

inline void unsynchronized_pool_resource::do_deallocate (void* ptr, std::size_t bytes, std::size_t align)
{
    if (is_oversized (bytes, align))
        oversized.deallocate_chunk (ptr, upstream);
    else
        which_pool (bytes).deallocate (ptr);
}

inline bool unsynchronized_pool_resource::is_oversized (std::size_t bytes, std::size_t align) const
{
    return bytes > opts.largest_required_pool_block || align > alignof(std::max_align_t);
}

} // namespace cradle::pmr

PMR_DIAGNOSTIC_POP
