#pragma once
// Merge sort written by hand (stable, O(n log n) time, O(n) extra space).
// It sorts a vector of POINTERS to accounts, so the linked list itself is never
// modified or reordered - the original data stays intact.
#include <cstddef>
#include <vector>

namespace sorting {

template <typename T, typename Less>
void mergeRange(std::vector<T>& items, std::vector<T>& buffer, std::size_t lo, std::size_t mid,
                std::size_t hi, Less less) {
    std::size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        // Take from the right only if strictly smaller -> equal items keep their order (stable).
        if (less(items[j], items[i])) buffer[k++] = items[j++];
        else buffer[k++] = items[i++];
    }
    while (i < mid) buffer[k++] = items[i++];
    while (j < hi) buffer[k++] = items[j++];
    for (std::size_t x = lo; x < hi; ++x) items[x] = buffer[x];
}

template <typename T, typename Less>
void mergeSortRange(std::vector<T>& items, std::vector<T>& buffer, std::size_t lo, std::size_t hi,
                    Less less) {
    if (hi - lo < 2) return;
    const std::size_t mid = lo + (hi - lo) / 2;
    mergeSortRange(items, buffer, lo, mid, less);
    mergeSortRange(items, buffer, mid, hi, less);
    mergeRange(items, buffer, lo, mid, hi, less);
}

template <typename T, typename Less>
void mergeSort(std::vector<T>& items, Less less) {
    if (items.size() < 2) return;
    std::vector<T> buffer(items.size());
    mergeSortRange(items, buffer, 0, items.size(), less);
}

}  // namespace sorting
