#include "utils.hpp"

#include "libmem/libmem.h"

#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <string>

FILE* Utils::logFile;

void Utils::log(const char* msg)
{
	log(msg, nullptr);
}

void Utils::init()
{
	char path[255];
	const char* home = getenv("HOME");
	if (!home)
		exit(1);

	strcat(path, home);
	strcat(path, "/.SLSsteam.log");

	logFile = fopen(path, "w");
	
	if (!logFile)
		exit(1);
}

lm_address_t Utils::searchSignature(const char* name, const char* signature, lm_module_t module, SigFollowMode mode)
{
	lm_address_t address = LM_SigScan(signature, module.base, module.size);
	if (address == LM_ADDRESS_BAD)
	{
		Utils::log("Unable to find signature for %s!\n", name);
	}
	else
	{
		switch (mode)
		{
			case SigFollowMode::Relative:
				Utils::log("Resolving relative of %s at %p\n", name, address);
				address = Utils::getJmpTarget(address);
				break;

			case SigFollowMode::PrologueUpwards:
				Utils::log("Searching function prologue of %s from %p\n", name, address);
				address = Utils::findPrologue(address);
				break;

			default:
				break;
		}

		Utils::log("%s at %p\n", name, address);
	}

	return address;
}

lm_address_t Utils::searchSignature(const char* name, const char* signature, lm_module_t module)
{
	return searchSignature(name, signature, module, SigFollowMode::None);
}

lm_address_t Utils::getJmpTarget(lm_address_t address)
{
	lm_inst_t inst;
	if (!LM_Disassemble(address, &inst)) //Should not happen if we land in a code section
	{
		Utils::log("Failed to disassemble code at %p!");
		return LM_ADDRESS_BAD;
	}

	Utils::log("Resolved to %s %s\n", inst.mnemonic, inst.op_str);

	if (strcmp(inst.mnemonic, "jmp") != 0 && strcmp(inst.mnemonic, "call") != 0)
		return LM_ADDRESS_BAD;

	return std::stoul(inst.op_str, nullptr, 16);
}

lm_address_t Utils::findPrologue(lm_address_t address)
{
	constexpr unsigned int scanSize = 0x1000; 
	constexpr lm_byte_t bytes[] = { 0x56, 0x57, 0xe5, 0x89, 0x55 }; //Reverse order since we're searching upwards
	constexpr lm_byte_t bytesSize = sizeof(bytes) / sizeof(bytes[0]);

	for(unsigned int i = 0u; i < scanSize; i++)
	{
		bool found = true;
		for(unsigned int j = 0u; j < bytesSize; j++)
		{
			if (*reinterpret_cast<lm_byte_t*>(address - i - j) != bytes[j])
			{
				found = false;
				break;
			}
		}

		if (found)
		{
			lm_address_t prol = address - i - bytesSize + 1; //Add 1 byte back since bytesSize would be to big otherwise
			Utils::log("Prologue found at %p\n", prol);
			return prol;
		}
	}

	Utils::log("Unable to find prologue after going up %p bytes!\n", scanSize);
	return LM_ADDRESS_BAD;
}
