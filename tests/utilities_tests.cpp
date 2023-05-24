//==============================================================================
// Copyright (c) 2019-2023 CradleApps, LLC - All Rights Reserved
//==============================================================================

#include <memory>
#include "realtime_memory/utilities.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_random.hpp>
#include <catch2/generators/catch_generators_range.hpp>

//==============================================================================
TEST_CASE ("merge sort linked lists")
{
    struct node
    {
        node* next = nullptr;
        int data = 0;

        explicit node (int d) : data (d) {}
    };

    const int listTailLength = 1000;
    auto rand = Catch::Generators::random<int> (0, 100000);

    // Build the list
    auto head = new node (rand.get());
    auto current = head;

    auto deleter = std::vector<std::unique_ptr<node>>();
    deleter.emplace_back (head);

    for (int i = 0; i < listTailLength; ++i, rand.next())
    {
        current->next = new node (rand.get());
        current = current->next;
        deleter.emplace_back (current);
    }

    const auto lowestFirst  = [] (node* a, node* b) { return a->data < b->data; };
    const auto highestFirst = [] (node* a, node* b) { return a->data > b->data; };

    SECTION ("Sort lowest first")
    {
        head = cradle::pmr::merge_sort_list (head, lowestFirst);

        int numListPairs = 0;
        int numSortedPairs = 0;

        for (auto n = head; n != nullptr && n->next != nullptr; n = n->next)
        {
            numSortedPairs += n->data <= n->next->data ? 1 : 0;
            ++numListPairs;
        }

        CHECK (numListPairs == listTailLength);
        CHECK (numSortedPairs == listTailLength);
    }

    SECTION ("Sort highest first")
    {
        head = cradle::pmr::merge_sort_list (head, highestFirst);

        int numListPairs = 0;
        int numSortedPairs = 0;

        for (auto n = head; n != nullptr && n->next != nullptr; n = n->next)
        {
            numSortedPairs += n->data >= n->next->data ? 1 : 0;
            ++numListPairs;
        }

        CHECK (numListPairs == listTailLength);
        CHECK (numSortedPairs == listTailLength);
    }
}
