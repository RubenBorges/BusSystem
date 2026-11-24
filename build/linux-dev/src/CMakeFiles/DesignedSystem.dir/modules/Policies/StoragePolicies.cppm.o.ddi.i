# 0 "/home/boopy/dev/Projects/DesignedSystem/src/modules/Policies/StoragePolicies.cppm"
# 1 "/home/boopy/dev/Projects/DesignedSystem/build/linux-dev//"
# 0 "<built-in>"
# 0 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3
# 0 "<command-line>" 2
# 1 "/home/boopy/dev/Projects/DesignedSystem/src/modules/Policies/StoragePolicies.cppm"
export module StoragePolicies;

import std;

export template <typename T>
struct VectorPolicy {

    std::vector<T> items;

    void add(T item) {

        items.push_back(std::move(item));
    }

    template <typename Predicate>
    void remove_if(Predicate&& predicate) {

        std::erase_if(items, std::forward<Predicate>(predicate));
    }

    template <typename Function>
    void for_each(Function&& function) const {

        for (const auto& item : items) {
            function(item);
        }
    }
};
