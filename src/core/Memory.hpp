#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>

namespace elm::memory {

	template<typename T>
	class Allocator
	{
	public:
		using value_type = T;

		Allocator() noexcept = default;

		template<typename U>
		Allocator(const Allocator<U>&) noexcept {}

		[[nodiscard]] T* allocate(std::size_t count)
		{
			if (count > static_cast<std::size_t>(-1) / sizeof(T))
				throw std::bad_alloc{};

			if (void* memory = std::malloc(count * sizeof(T)))
				return static_cast<T*>(memory);

			throw std::bad_alloc{};
		}

		void deallocate(T* ptr, std::size_t) noexcept
		{
			std::free(ptr);
		}
	};

	template<typename T, typename U>
	bool operator==(const Allocator<T>&, const Allocator<U>&) noexcept
	{
		return true;
	}

	template<typename T, typename U>
	bool operator!=(const Allocator<T>&, const Allocator<U>&) noexcept
	{
		return false;
	}
}