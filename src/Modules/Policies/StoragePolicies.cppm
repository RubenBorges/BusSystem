export module StoragePolicies;

import std;

export template <typename T>
struct VectorPolicy {
    // Stores all values managed by this policy in a vector.
    std::vector<T> items;

    void add(T item) {
        // Move the item so callbacks and other movable values are stored efficiently.
        items.push_back(std::move(item));
    }

    template <typename Predicate>
    void remove_if(Predicate&& predicate) {
        // Erase subscribers whose identifiers match the supplied predicate.
        std::erase_if(items, std::forward<Predicate>(predicate));
    }

    template <typename Function>
    void for_each(Function&& function) const {
        // Iterate through subscribers without exposing the storage implementation.
        for (const auto& item : items) {
            function(item);
        }
    }
};
