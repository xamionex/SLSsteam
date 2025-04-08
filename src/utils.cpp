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

lm_address_t Utils::searchSignature(const char* name, const char* signature, lm_module_t module, bool resolveRelative)
{
	lm_address_t address = LM_SigScan(signature, module.base, module.size);
	if (address == LM_ADDRESS_BAD)
	{
		Utils::log("Unable to find signature for %s!\n", name);
	}
	else
	{
		if (resolveRelative)
		{
			Utils::log("Resolving relative of %s at %p\n", name, address);
			address = Utils::getJmpTarget(address);
		}

		Utils::log("%s at %p\n", name, address);
	}

	return address;
}

lm_address_t Utils::searchSignature(const char* name, const char* signature, lm_module_t module)
{
	return searchSignature(name, signature, module, false);
}

lm_address_t Utils::getJmpTarget(lm_address_t address)
{
	lm_inst_t inst;
	//TODO: Maybe add check if disassemble succeeded
	LM_Disassemble(address, &inst);
	Utils::log("Resolved to %s %s\n", inst.mnemonic, inst.op_str);

	if (strcmp(inst.mnemonic, "jmp") != 0 && strcmp(inst.mnemonic, "call") != 0)
		return LM_ADDRESS_BAD;

	return std::stoul(inst.op_str, nullptr, 16);
}
