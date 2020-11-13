// Reference crunch 
// Add by tongkuisu

#pragma once
#include "vector"

namespace basisu
{
	// The basis library assumes all allocation blocks have at least CRND_MIN_ALLOC_ALIGNMENT alignment.
	const uint32_t CRND_MIN_ALLOC_ALIGNMENT = sizeof(uint32_t) * 2U;
	const uint32_t cIntBits = 32U;
}

namespace basisu
{
	void* basis_malloc(size_t size);
	void* basis_malloc(size_t size, size_t* pActual_size);
	void* basis_realloc(void* p, size_t size, size_t* pActual_size, bool movable);
	void  basis_free(void* p);
	size_t basis_msize(void* p);


	typedef void*(*basis_realloc_func)(void* p, size_t size, size_t* pActual_size, bool movable, void* pUser_data);
	typedef size_t(*basis_msize_func)(void* p, void* pUser_data);

	// Call this to use your own memory allocator
	void basis_set_memory_callbacks(basis_realloc_func pRealloc, basis_msize_func pMSize, void* pUser_data);
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
				throw std::bad_alloc();
			}
				
			if (auto p = static_cast<T*>(basis_malloc(n * sizeof(T)))) 
			{
				return p;
			}

			throw std::bad_alloc();
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
	class MVector :public std::vector<T, Mallocator<T>>
	{
		typedef typename std::vector<T, Mallocator<T>>::iterator MIterator;
	public:
		MVector() :std::vector<T, Mallocator<T>>() {}
		MVector(int count) :std::vector<T, Mallocator<T>>(count) {}
		MVector(int count, const T val) :std::vector<T, Mallocator<T>>(count, val) {}
		MVector(MIterator first, MIterator end) :std::vector<T, Mallocator<T>>(first, end) {}
		MVector(const MVector& x) :std::vector<T, Mallocator<T>>(x) {}
	};
}
