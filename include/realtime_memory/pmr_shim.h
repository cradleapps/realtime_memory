//==============================================================================
// Copyright (c) 2019-2026 CradleApps, LLC - All Rights Reserved
//==============================================================================
#pragma once
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

/** On macOS std::pmr is available but requires the libc++ from macOS 14.
 *
 *  This implementation ensures that memory_resource & polymorphic_allocator
 *  are available on macOS 10.14 - 13 as well.
 *
 *  Care is taken to ensure that everything is header-only to ensure all
 *  symbols including the vtables are present in consuming object
 *  files rather than linking on load.
 */
namespace rtm_pmr
{
class memory_resource;
}

namespace cradle::pmr
{
/** Defined in memory_resources.h. Declared here so that a default-constructed
 *  polymorphic_allocator picks up the same default resource as the rest of the
 *  library, rather than keeping a second, independent one.
 */
rtm_pmr::memory_resource* get_default_resource() noexcept;
} // namespace cradle::pmr

namespace rtm_pmr
{
//==============================================================================
/** An abstract interface for classes that encapsulate memory resources. */
class memory_resource
{
public:
    memory_resource() = default;
    memory_resource (const memory_resource&) = default;
    memory_resource& operator= (const memory_resource&) = default;

    /** Defined inline, so this class has no Itanium ABI "key function".
     *  The vtable is then created in any consuming object file and deduplicated
     *  at link time.
     */
    virtual ~memory_resource() = default;

    void* allocate (std::size_t bytes, std::size_t alignment = alignof (std::max_align_t))
    {
        return do_allocate (bytes, alignment);
    }

    void deallocate (void* p, std::size_t bytes, std::size_t alignment = alignof (std::max_align_t))
    {
        do_deallocate (p, bytes, alignment);
    }

    bool is_equal (const memory_resource& other) const noexcept
    {
        return do_is_equal (other);
    }

private:
    virtual void* do_allocate (std::size_t bytes, std::size_t alignment) = 0;
    virtual void do_deallocate (void* p, std::size_t bytes, std::size_t alignment) = 0;
    virtual bool do_is_equal (const memory_resource& other) const noexcept = 0;
};

inline bool operator== (const memory_resource& a, const memory_resource& b) noexcept
{
    return &a == &b || a.is_equal (b);
}

inline bool operator!= (const memory_resource& a, const memory_resource& b) noexcept
{
    return ! (a == b);
}

//==============================================================================
/** An allocator that routes its allocations through a memory_resource, and
 *  that performs uses-allocator construction so that allocator-aware elements
 *  inherit the resource.
 */
template <class T>
class polymorphic_allocator
{
public:
    using value_type = T;

    polymorphic_allocator() noexcept
        : m_resource (cradle::pmr::get_default_resource())
    {
    }

    /** Intentionally implicit, matching std::pmr::polymorphic_allocator.
     */
    polymorphic_allocator (memory_resource* r) noexcept
        : m_resource (r)
    {
    }

    polymorphic_allocator (const polymorphic_allocator&) = default;

    template <class U>
    polymorphic_allocator (const polymorphic_allocator<U>& other) noexcept
        : m_resource (other.resource())
    {
    }

    /** Deleted, as in the standard so an allocator's resource is fixed for its lifetime.
     */
    polymorphic_allocator& operator= (const polymorphic_allocator&) = delete;

    T* allocate (std::size_t n)
    {
        if (n > max_size())
            throw std::length_error ("polymorphic_allocator<T>::allocate(n) exceeds max_size()");

        return static_cast<T*> (m_resource->allocate (n * sizeof (T), alignof (T)));
    }

    void deallocate (T* p, std::size_t n)
    {
        m_resource->deallocate (p, n * sizeof (T), alignof (T));
    }

    //==============================================================================
    template <class U, class... Args>
    void construct (U* p, Args&&... args)
    {
        construct_using_allocator (p, std::forward<Args> (args)...);
    }

    template <class T1, class T2, class... Args1, class... Args2>
    void construct (std::pair<T1, T2>* p,
                    std::piecewise_construct_t,
                    std::tuple<Args1...> x,
                    std::tuple<Args2...> y)
    {
        ::new (static_cast<void*> (p))
            std::pair<T1, T2> (std::piecewise_construct,
                               allocator_args<T1> (std::move (x)),
                               allocator_args<T2> (std::move (y)));
    }

    template <class T1, class T2>
    void construct (std::pair<T1, T2>* p)
    {
        construct (p, std::piecewise_construct, std::tuple<>(), std::tuple<>());
    }

    template <class T1, class T2, class U, class V>
    void construct (std::pair<T1, T2>* p, U&& x, V&& y)
    {
        construct (p,
                   std::piecewise_construct,
                   std::forward_as_tuple (std::forward<U> (x)),
                   std::forward_as_tuple (std::forward<V> (y)));
    }

    template <class T1, class T2, class U, class V>
    void construct (std::pair<T1, T2>* p, const std::pair<U, V>& pr)
    {
        construct (p,
                   std::piecewise_construct,
                   std::forward_as_tuple (pr.first),
                   std::forward_as_tuple (pr.second));
    }

    template <class T1, class T2, class U, class V>
    void construct (std::pair<T1, T2>* p, std::pair<U, V>&& pr)
    {
        construct (p,
                   std::piecewise_construct,
                   std::forward_as_tuple (std::forward<U> (pr.first)),
                   std::forward_as_tuple (std::forward<V> (pr.second)));
    }

    template <class U>
    void destroy (U* p)
    {
        p->~U();
    }

    polymorphic_allocator select_on_container_copy_construction() const
    {
        return polymorphic_allocator();
    }

    memory_resource* resource() const noexcept { return m_resource; }

private:
    static constexpr std::size_t max_size() noexcept
    {
        return std::numeric_limits<std::size_t>::max() / sizeof (T);
    }

    template <class U, class... Args>
    void construct_using_allocator (U* p, Args&&... args)
    {
        if constexpr (! std::uses_allocator<U, polymorphic_allocator>::value)
        {
            static_assert (std::is_constructible<U, Args...>::value,
                           "U must be constructible from Args");

            ::new (static_cast<void*> (p)) U (std::forward<Args> (args)...);
        }
        else if constexpr (std::is_constructible<U, std::allocator_arg_t, polymorphic_allocator&, Args...>::value)
        {
            ::new (static_cast<void*> (p)) U (std::allocator_arg, *this, std::forward<Args> (args)...);
        }
        else
        {
            static_assert (std::is_constructible<U, Args..., polymorphic_allocator&>::value,
                           "U uses an allocator but is not constructible from Args plus this allocator");

            ::new (static_cast<void*> (p)) U (std::forward<Args> (args)..., *this);
        }
    }

    /** Returns the tuple of arguments to build a U with, with this allocator
     *  spliced in wherever U expects it (leading, trailing, or not at all).
     */
    template <class U, class... Args>
    auto allocator_args (std::tuple<Args...>&& t)
    {
        if constexpr (! std::uses_allocator<U, polymorphic_allocator>::value)
        {
            return std::move (t);
        }
        else if constexpr (std::is_constructible<U, std::allocator_arg_t, polymorphic_allocator&, Args...>::value)
        {
            return std::tuple_cat (std::tuple<std::allocator_arg_t, polymorphic_allocator&> (std::allocator_arg, *this),
                                   std::move (t));
        }
        else
        {
            static_assert (std::is_constructible<U, Args..., polymorphic_allocator&>::value,
                           "U uses an allocator but is not constructible from Args plus this allocator");

            return std::tuple_cat (std::move (t), std::tuple<polymorphic_allocator&> (*this));
        }
    }

    memory_resource* m_resource;
};

template <class T1, class T2>
bool operator== (const polymorphic_allocator<T1>& a, const polymorphic_allocator<T2>& b) noexcept
{
    return *a.resource() == *b.resource();
}

template <class T1, class T2>
bool operator!= (const polymorphic_allocator<T1>& a, const polymorphic_allocator<T2>& b) noexcept
{
    return ! (a == b);
}

} // namespace rtm_pmr
