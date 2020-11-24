// Add by tongkuisu

#pragma once
#include "vector"
#include <stddef.h>
#include <limits>


namespace basisu
{
	void* basis_malloc(size_t size);
	void  basis_free(void* p);

	typedef void*(*basis_malloc_func)(size_t size);
	typedef void (*basis_free_func)(void* p);

	// Call this to use your own memory allocator
	void basis_set_memory_callbacks(basis_malloc_func pMalloc, basis_free_func pFree);
}

namespace basisu
{
	template <class T>
	struct Mallocator
	{
		typedef T value_type;

		Mallocator() = default;
		template <class U> constexpr Mallocator(const Mallocator <U>&) {}

		T* allocate(std::size_t n)
		{
			if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
			{
				return nullptr;
			}
				
			if (auto p = static_cast<T*>(basis_malloc(n * sizeof(T)))) 
			{
				return p;
			}

			return nullptr;
		}

		void deallocate(T* p, std::size_t n)
		{
			basis_free(p);
		}
	};

	template <class T, class U>
	bool operator==(const Mallocator <T>&, const Mallocator <U>&) { return true; }
	template <class T, class U>
	bool operator!=(const Mallocator <T>&, const Mallocator <U>&) { return false; }

	template <class T>
	using MVector = std::vector<T, Mallocator<T>>;
}
