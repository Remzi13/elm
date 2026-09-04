#pragma once

#include "core/Memory.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <utility>
#include <functional>

namespace elm {
    using String = std::basic_string<char, std::char_traits<char>, memory::Allocator<char>>;
    using StringView = std::basic_string_view<char, std::char_traits<char>>;

    template<typename T>
    using Vector = std::vector<T, memory::Allocator<T>>;

    template<typename K, typename V>
    using UnorderedMap = std::unordered_map<K, V, std::hash<K>, std::equal_to<K>, memory::Allocator<std::pair<const K, V>>>;

    template<typename T>
    using SharedPtr = std::shared_ptr<T>;

    template<typename T, typename... Args>
    SharedPtr<T> MakeShared(Args&&... args) {
        return std::allocate_shared<T>(memory::Allocator<T>(), std::forward<Args>(args)...);
    }

    template<typename T>
    using UniquePtr = std::unique_ptr<T, std::function<void(void*)>>;

    template<typename T>
    UniquePtr<T> MakeUnique(T* ptr) {
        memory::Allocator<T> allocator;
        return UniquePtr<T>(ptr, [allocator](void* value) mutable {
            if (value) {
                auto* typedValue = static_cast<T*>(value);
                std::destroy_at(typedValue);
                allocator.deallocate(typedValue, 1);
            }
        });
    }

    template<typename T, typename... Args>
    UniquePtr<T> MakeUnique(Args&&... args) {
        memory::Allocator<T> allocator;
        T* ptr = allocator.allocate(1);

        try {
            std::construct_at(ptr, std::forward<Args>(args)...);
        } catch (...) {
            allocator.deallocate(ptr, 1);
            throw;
        }

        return UniquePtr<T>(ptr, [allocator](void* value) mutable {
            if (value) {
                auto* typedValue = static_cast<T*>(value);
                std::destroy_at(typedValue);
                allocator.deallocate(typedValue, 1);
            }
        });
    }
}
