#pragma once
constexpr int MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2;
inline unsigned heap_caps_get_free_size(int) { return 0; }
inline unsigned heap_caps_get_largest_free_block(int) { return 0; }
inline unsigned heap_caps_get_minimum_free_size(int) { return 0; }
