#include "core/Memory.hpp"

#include <atomic>

namespace elm::memory {
    
    namespace {
        std::atomic<int64_t> allocatedBytes{ 0 };     
    }

    void* allocate_impl(size_t size)
    {
        if (void* memory = std::malloc(size))
        {
            allocatedBytes += size;
            return memory;
        }

        throw std::bad_alloc{};
    }

    void deallocate_impl(void* ptr, size_t size)
    {         
        allocatedBytes -= size;
        std::free(ptr);        
    }

    Statistics getStatistic()
    {        
        return Statistics{ allocatedBytes.load() };
    }
}