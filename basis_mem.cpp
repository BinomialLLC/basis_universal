// Reference crunch 
// Add by tongkuisu
#ifndef BASISU_NO_ITERATOR_DEBUG_LEVEL
#if defined(_DEBUG) || defined(DEBUG)
#define _ITERATOR_DEBUG_LEVEL 1
#define _SECURE_SCL 1
#else
#define _SECURE_SCL 0
#define _ITERATOR_DEBUG_LEVEL 0
#endif
#endif

#include "basis_mem.h"
#include <stdlib.h>
#include <stdio.h>
#ifdef WIN32
#include <memory.h>
#else
#include <malloc.h>
#endif
#include <stdarg.h>
#include <new> // needed for placement new, _msize, _expand
#include <assert.h>


namespace basisu
{
	const uint32 MAX_POSSIBLE_BLOCK_SIZE = 0x7FFF0000U;

	static void* basis_default_realloc(void* p, size_t size, size_t* pActual_size, bool movable, void* pUser_data)
	{
		pUser_data;

		void* p_new;

		if (!p)
		{
			p_new = ::malloc(size);

			if (pActual_size)
			{
#ifdef WIN32
				*pActual_size = p_new ? ::_msize(p_new) : 0;
#else
				*pActual_size = p_new ? malloc_usable_size(p_new) : 0;
#endif
			}
		}
		else if (!size)
		{
			::free(p);
			p_new = NULL;

			if (pActual_size)
				*pActual_size = 0;
		}
		else
		{
			void* p_final_block = p;
#ifdef WIN32
			p_new = ::_expand(p, size);
#else
			p_new = NULL;
#endif

			if (p_new)
				p_final_block = p_new;
			else if (movable)
			{
				p_new = ::realloc(p, size);

				if (p_new)
					p_final_block = p_new;
			}

			if (pActual_size)
			{
#ifdef WIN32
				*pActual_size = ::_msize(p_final_block);
#else
				*pActual_size = ::malloc_usable_size(p_final_block);
#endif
			}
	}

		return p_new;
}

	static size_t basis_default_msize(void* p, void* pUser_data)
	{
		pUser_data;
#ifdef WIN32
		return p ? _msize(p) : 0;
#else
		return p ? malloc_usable_size(p) : 0;
#endif
	}

	static basis_realloc_func    g_pRealloc = basis_default_realloc;
	static basis_msize_func      g_pMSize = basis_default_msize;
	static void*               g_pUser_data;

	void basis_set_memory_callbacks(basis_realloc_func pRealloc, basis_msize_func pMSize, void* pUser_data)
	{
		if ((!pRealloc) || (!pMSize))
		{
			g_pRealloc = basis_default_realloc;
			g_pMSize = basis_default_msize;
			g_pUser_data = NULL;
		}
		else
		{
			g_pRealloc = pRealloc;
			g_pMSize = pMSize;
			g_pUser_data = pUser_data;
		}
	}

	void* basis_malloc(size_t size)
	{
		return basis_malloc(size, nullptr);
	}

	void* basis_malloc(size_t size, size_t* pActual_size)
	{
		size = (size + sizeof(uint32) - 1U) & ~(sizeof(uint32) - 1U);
		if (!size)
			size = sizeof(uint32);

		if (size > MAX_POSSIBLE_BLOCK_SIZE)
		{
			return NULL;
		}

		size_t actual_size = size;
		uint8* p_new = static_cast<uint8*>((*g_pRealloc)(NULL, size, &actual_size, true, g_pUser_data));

		if (pActual_size)
			*pActual_size = actual_size;

		if ((!p_new) || (actual_size < size))
		{
			return NULL;
		}

		return p_new;
	}

	void* basis_realloc(void* p, size_t size, size_t* pActual_size, bool movable)
	{
		if (size > MAX_POSSIBLE_BLOCK_SIZE)
		{
			return NULL;
		}

		size_t actual_size = size;
		void* p_new = (*g_pRealloc)(p, size, &actual_size, movable, g_pUser_data);

		if (pActual_size)
			*pActual_size = actual_size;

		return p_new;
	}

	void basis_free(void* p)
	{
		if (!p)
			return;

		(*g_pRealloc)(p, 0, NULL, true, g_pUser_data);
	}

	size_t basis_msize(void* p)
	{
		if (!p)
			return 0;

		return (*g_pMSize)(p, g_pUser_data);
	}
}
