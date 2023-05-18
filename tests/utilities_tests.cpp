//==============================================================================
// Copyright (c) 2019-2022 CradleApps, LLC - All Rights Reserved
//
// This file is part of the Cradle Engine. Unauthorised copying and
// redistribution is strictly prohibited. Proprietary and confidential.
//==============================================================================

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

    const int listLength = 1000;
    auto rand = Catch::Generators::random<int> (0, 100000);

    // Build the list
    auto head = new node (rand.get());
    auto current = head;

    for (int i = 0; i < listLength; ++i, rand.next())
    {
        current->next = new node (rand.get());
        current = current->next;
    }

    const auto lowestFirst  = [] (node* a, node* b) { return a->data < b->data; };
    const auto highestFirst = [] (node* a, node* b) { return a->data > b->data; };

    SECTION ("Sort lowest first")
    {
        head = cradle::pmr::merge_sort_list (head, lowestFirst);

        int numInList = 0;
        int numSorted = 0;

        for (auto n = head; n != nullptr && n->next != nullptr; n = n->next)
        {
            numSorted += n->data <= n->next->data ? 1 : 0;
            ++numInList;
        }

        CHECK (numInList == listLength);
        CHECK (numSorted == listLength);
    }

    SECTION ("Sort highest first")
    {
        head = cradle::pmr::merge_sort_list (head, highestFirst);

        int numInList = 0;
        int numSorted = 0;

        for (auto n = head; n != nullptr && n->next != nullptr; n = n->next)
        {
            numSorted += n->data >= n->next->data ? 1 : 0;
            ++numInList;
        }

        CHECK (numInList == listLength);
        CHECK (numSorted == listLength);
    }

    // Free the list
    while (head != nullptr)
    {
        auto next = head->next;
        delete head;
        head = next;
    }
}
