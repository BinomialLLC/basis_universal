#include "basis_mem.h"

namespace basisu
{
	static void* basis_default_malloc_func(size_t size)
	{
		return ::operator new(size);
	}

	static void basis_default_free_func(void* p)
	{
		::operator delete(p);
	}


	static basis_malloc_func g_pMalloc = basis_default_malloc_func;
	static basis_free_func g_pFree = basis_default_free_func;

	void* basis_malloc(size_t size)
	{
		return (*g_pMalloc)(size);
	}

	void basis_free(void* p)
	{
		if (!p)
			return;

		g_pFree(p);
	}

	void basis_set_memory_callbacks(basis_malloc_func pMalloc, basis_free_func pFree)
	{
		if ((!pMalloc) || (!pFree))
		{
			g_pMalloc = basis_default_malloc_func;
			g_pFree = basis_default_free_func;
		}
		else
		{
			g_pMalloc = pMalloc;
			g_pFree = pFree;
		}
	}
}
