//==============================================================================
// Copyright (c) 2019-2022 CradleApps, LLC - All Rights Reserved
//==============================================================================

#pragma once
#include "pmr_includes.h"
#include "memory_resources.h"

PMR_DIAGNOSTIC_PUSH

namespace cradle::pmr
{
/** A simple alias would trigger 'undefined symbols for get_default_resource()' on ARM. */
template <typename T>
class polymorphic_allocator : public std_pmr::polymorphic_allocator<T>
{
public:
    polymorphic_allocator() : base (get_default_resource()) {}
    polymorphic_allocator (memory_resource* r) : base (r) {}
    polymorphic_allocator (polymorphic_allocator& other) : base (other) {}

    template <class Tp>
    polymorphic_allocator (const polymorphic_allocator<Tp>& other) noexcept
        : base (other.resource())
    {}

    using base = std_pmr::polymorphic_allocator<T>;
};

/** propagating_allocator is a polymorphic allocator similar to polymorphic_allocator
 *  but which propagates when containers (strings, vectors, etc.) are copied and moved.
 *
 *  It is designed to be used in a realtime context, when we must ensure that all
 *  object allocations are from a non-locking memory resource.
 *
 *  The container aliases defined in <containers.h> use this allocator type.
 */
template <class ValueType>
class propagating_allocator
{
public:
    using value_type = ValueType;

    propagating_allocator (cradle::pmr::memory_resource* _resource)
        : m_resource (_resource)
    {
    }

    propagating_allocator (const propagating_allocator&) = default;

    template <class OtherValueType>
    propagating_allocator (const propagating_allocator<OtherValueType>& other) noexcept
        : m_resource (other.resource())
    {
    }

    propagating_allocator& operator= (const propagating_allocator& other)
    {
        this->m_resource = other.m_resource;
        return *this;
    }

    propagating_allocator select_on_container_copy_construction() const
    {
        return *this;
    }

    ValueType* allocate (const size_t count)
    {
        void* ptr = m_resource->allocate (sizeof (ValueType) * (count), alignof (ValueType));
        return static_cast<ValueType*> (ptr);
    }

    void deallocate (ValueType* ptr, const size_t count)
    {
        m_resource->deallocate (ptr, count * sizeof (ValueType), alignof (ValueType));
    }

    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;

    template <class OtherType, class... ArgTypes>
    void construct (OtherType* ptr, ArgTypes&&... _Args)
    {
        cradle::pmr::polymorphic_allocator<ValueType> alloc (m_resource);
        alloc.construct (ptr, std::forward<ArgTypes> (_Args)...);
    }

    template <class OtherType>
    void destroy (OtherType* ptr)
    {
        ptr->~OtherType();
    }

    cradle::pmr::memory_resource* resource() const
    {
        return m_resource;
    }

private:
    cradle::pmr::memory_resource* m_resource = cradle::pmr::get_default_resource();
};

template <class ValueType1, class ValueType2>
bool operator== (const propagating_allocator<ValueType1>& alloc1,
                 const propagating_allocator<ValueType2>& alloc2) noexcept
{
    return alloc1.resource()->is_equal (*alloc2.resource());
}

template <class ValueType1, class ValueType2>
bool operator!= (const propagating_allocator<ValueType1>& alloc1,
                 const propagating_allocator<ValueType2>& alloc2) noexcept
{
    return ! (alloc1 == alloc2);
}

} // cradle::pmr

PMR_DIAGNOSTIC_POP
