#include "utils.hpp"
#include "log.hpp"

#include "libmem/libmem.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <ios>
#include <pthread.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/sha.h>

std::vector<std::string> Utils::strsplit(char *str, const char *delimeter)
{
	auto splits = std::vector<std::string>();

	char* split = strtok(str, delimeter);
	splits.emplace(splits.end(), std::string(split));

	while(split)
	{
		split = strtok(nullptr, delimeter);
		if (!split)
		{
			break;
		}

		splits.emplace(splits.end(), std::string(split));
	}

	return splits;
}

std::string Utils::getFileSHA256(const char *filePath)
{
	std::ifstream fs(filePath, std::ios::binary);
	if (!fs.is_open())
	{
		//TODO: Read more about error types in C++ :)
		throw std::runtime_error("Unable to read file!");
	}

	std::vector<unsigned char> bytes(std::istreambuf_iterator<char>(fs), {});
	unsigned char sha256Bytes[SHA256_DIGEST_LENGTH];
	SHA256(bytes.data(), bytes.size(), sha256Bytes);

	std::stringstream sha256;
	for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
	{
		sha256 << std::hex << std::setw(2) << std::setfill('0') << (int)sha256Bytes[i];
	}

	fs.close();
	return sha256.str();
}

lm_address_t Utils::searchSignature(const char* name, const char* signature, lm_module_t module, SigFollowMode mode)
{
	lm_address_t address = LM_SigScan(signature, module.base, module.size);
	if (address == LM_ADDRESS_BAD)
	{
		g_pLog->debug("Unable to find signature for %s!\n", name);
	}
	else
	{
		switch (mode)
		{
			case SigFollowMode::Relative:
				g_pLog->debug("Resolving relative of %s at %p\n", name, address);
				address = Utils::getJmpTarget(address);
				break;

			case SigFollowMode::PrologueUpwards:
				g_pLog->debug("Searching function prologue of %s from %p\n", name, address);
				address = Utils::findPrologue(address);
				break;

			default:
				break;
		}

		g_pLog->info("%s at %p\n", name, address);
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
		g_pLog->debug("Failed to disassemble code at %p!");
		return LM_ADDRESS_BAD;
	}

	g_pLog->debug("Resolved to %s %s\n", inst.mnemonic, inst.op_str);

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
			g_pLog->debug("Prologue found at %p\n", prol);
			return prol;
		}
	}

	g_pLog->debug("Unable to find prologue after going up %p bytes!\n", scanSize);
	return LM_ADDRESS_BAD;
}

bool Utils::fixPICThunkCall(const char* name, lm_address_t fn, lm_address_t tramp)
{
	g_pLog->debug("Fixing PIC thunks for %s's trampoline\n", name);
	constexpr unsigned int maxBytes = 0x5; //Minimum bytes needed to detour a function, so our tramp will at least be of this size
	
	lm_inst_t inst;
	for(unsigned int curTrampOffset = 0; curTrampOffset <= maxBytes; )
	{
		lm_address_t startAddress = tramp + curTrampOffset;

		if (!LM_Disassemble(startAddress, &inst))
		{
			g_pLog->debug("Unable to dissassemble code at %p\n", tramp + curTrampOffset);
			return false;
		}
		
		curTrampOffset += inst.size;
		g_pLog->debug("%p: %s %s\n", inst.address, inst.mnemonic, inst.op_str);
		
		if (strcmp(inst.mnemonic, "call") != 0)
			continue;

		//Calculate the call address manually with it's original location
		lm_address_t followAddress = fn + curTrampOffset + *reinterpret_cast<lm_address_t*>(startAddress + 1);
		bool isIPCThunk = true;
		char newInstr[sizeof(inst.mnemonic) + sizeof(inst.op_str)];

		for(unsigned int i = 0; i < 2; i++) //Dissassemble next 2 instructions and check if they're an actual IPC thunk call
		{
			if (!LM_Disassemble(followAddress, &inst))
			{
				g_pLog->debug("Unable to dissassemble code at %p\n", followAddress);
				return false;
			}

			followAddress += inst.size;

			g_pLog->debug("%p: %s %s\n", inst.address, inst.mnemonic, inst.op_str);

			//Can not declare in switch statement
			auto splits = std::vector<std::string>();
			lm_address_t retAddress = LM_ADDRESS_BAD;
			switch(i)
			{
				case 0:
					if (strcmp(inst.mnemonic, "mov") != 0)
						isIPCThunk = false;
					
					splits = strsplit(inst.op_str, ","); //Not checking for splits.size() since mov NEEDS a , somewhere
					retAddress = fn + curTrampOffset; //No need to add any bytes here, since i += inst.size in the outer loop takes care of that
					sprintf(newInstr, "%s %s, %p", inst.mnemonic, splits.at(0).c_str(), reinterpret_cast<void*>(retAddress));
					break;

				case 1:
					if (strcmp(inst.mnemonic, "ret") != 0)
						isIPCThunk = false;
					break;
			}

			if (!isIPCThunk)
				break;
		}

		if (!isIPCThunk)
			continue;

		if(!LM_Assemble(newInstr, &inst))
		{
			printf("Unable to assemble instruction %s!\n", newInstr);
			return false;
		}

		lm_prot_t oldProt;
		LM_ProtMemory(startAddress, inst.size, LM_PROT_XRW, &oldProt);
		LM_WriteMemory(startAddress, inst.bytes, inst.size);
		LM_ProtMemory(startAddress, inst.size, oldProt, nullptr);
		g_pLog->debug("Replaced PIC thunk call for %s at %p with %s\n", name, followAddress, newInstr);
		return true;
	}

	return false;
}
