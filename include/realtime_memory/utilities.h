//==============================================================================
// Copyright (c) 2019-2023 CradleApps, LLC - All Rights Reserved
//==============================================================================
#pragma once
#include <utility>

namespace cradle::pmr
{

/** Merge sort a singly linked list.
 *   - `list_node` must have a `next` pointer.
 *   - `compare_nodes_fn` should take two `list_node` pointers and
 *     return true if the first should be before the second in the list.
 *
 *  This is probably the fastest way of sorting a singly linked list
 *  without allocating auxiliary memory.
 *
 *  Inspired by this public-domain pseudocode:
 *  https://www.chiark.greenend.org.uk/~sgtatham/algorithms/listsort.html
 */
template <typename list_node, typename compare_nodes_fn>
list_node* merge_sort_list (list_node* list, compare_nodes_fn&& compare)
{
    if (list == nullptr)
        return nullptr;

    int chunk_size = 1;

    while (true)
    {
        list_node* p = std::exchange (list, nullptr);
        list_node* tail = nullptr;
        int num_merges = 0;

        while (p != nullptr)
        {
            ++num_merges;

            list_node* q = p;
            int q_size = chunk_size;
            int p_size = 0;

            for (int i = 0; i < chunk_size; ++i)
            {
                ++p_size;
                q = q->next;

                if (q == nullptr)
                    break;
            }

            list_node* next = nullptr;

            // merge the two lists, choosing nodes from p or q as appropriate.
            while (p_size > 0 || (q_size > 0 && q != nullptr))
            {
                if (p_size != 0 && (q_size == 0 || q == nullptr || compare (p, q)))
                {
                    next = p;
                    p = p->next;
                    --p_size;
                }
                else
                {
                    next = q;
                    q = q->next;
                    --q_size;
                }

                // add the next node to the merged list
                if (tail == nullptr)
                    list = next;
                else
                    tail->next = next;

                tail = next;
            }

            // both p and q have stepped `chunk_size' places along
            p = q;
        }

        tail->next = nullptr;

        // If we have done only one merge, we're finished.
        if (num_merges <= 1) // could be 0 if p is empty
            return list;

        // Otherwise repeat, merging lists twice the size
        chunk_size *= 2;
    }
}

}
