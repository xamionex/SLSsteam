#pragma once
#include "libmem/libmem.h"

#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>
#include <vector>

namespace Utils
{
	enum SigFollowMode
	{
		None,
		Relative,
		PrologueUpwards
	};

	std::vector<std::string> strsplit(char* str, const char* delimeter);
	std::string getFileSHA256(const char* filePath);

	lm_address_t searchSignature(const char* name, const char* signature, lm_module_t module, SigFollowMode);
	lm_address_t searchSignature(const char* name, const char* signature, lm_module_t module);

	lm_address_t getJmpTarget(lm_address_t address);
	lm_address_t findPrologue(lm_address_t address);

	//TODO: Create hooking wrapper that calls this automatically
	bool fixPICThunkCall(const char* name, lm_address_t fn, lm_address_t tramp);
	
	template<typename tFN, typename ...Args>
	constexpr auto callVFunc(unsigned int index, void* thisPtr, Args... args)
	{
		const auto fn = reinterpret_cast<tFN>(*(*reinterpret_cast<lm_address_t***>(thisPtr) + index));
		return fn(thisPtr, args...);
	}
}
