#pragma once
#include "libmem/libmem.h"

#include <cstdio>

namespace Utils
{
	extern FILE* logFile;

	//AFAIK variadic args do not work in cpp files, probably wrong though
	template<typename ...Args>
	void log(const char *fmt, Args... args)
	{
		fprintf(logFile, fmt, args...);
		fflush(logFile);
	}
	void log(const char* msg);

	void init();

	lm_address_t searchSignature(const char* name, const char* signature, lm_module_t module, bool resolveRelative);;
	lm_address_t searchSignature(const char* name, const char* signature, lm_module_t module);

	lm_address_t getJmpTarget(lm_address_t address);
	
	template<typename tFN, typename ...Args>
	constexpr auto callVFunc(unsigned int index, void* thisPtr, Args... args)
	{
		const auto fn = reinterpret_cast<tFN>(*(*reinterpret_cast<lm_address_t***>(thisPtr) + index));
		return fn(thisPtr, args...);
	}
}
