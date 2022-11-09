# Purpose

At time of writing (Nov 2022) clang's libc++ has still not implemented
the concrete `memory_resource` types defined by the standard in the
`std::pmr` namespace.

We have found a need for these in our development of realtime audio
software. This library offers the `memory_resource` types we have
found useful for realtime software, plus one extra resource that
is not offered in the standard (the `free_list_resource`).

| Type name                    | cradle::pmr   | std::pmr      | std::pmr (libc++) |
| ---------------------------- | ------------- | ------------- | ----------------- |
| monotonic_buffer_resource    | Yes           | Yes           | No                |
| unsynchronized_pool_resource | Yes           | Yes           | No                |
| synchronized_pool_resource   | No            | Yes           | No                |
| new_delete_resource          | Yes           | Yes           | No                |
| null_memory_resource         | Yes           | Yes           | No                |
| free_list_resource           | Yes           | No            | No                |

The library is licensed under the **MIT license**.
