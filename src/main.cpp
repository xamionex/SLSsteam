#include "config.hpp"
#include "globals.hpp"
#include "hooks.hpp"
#include "utils.hpp"

#include "libmem/libmem.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <link.h>
#include <pthread.h>
#include <cstdio>
#include <sstream>
#include <string>
#include <unistd.h>

#include <openssl/sha.h>

const char* SAFE_HASH = "075e8017db915e75b4e68cda3c363bad63afff0e033d4ace3fd3b27cc5e265d0";

bool removeSLSsteamFromEnvVar(const char* varName)
{
	char* var = getenv(varName);
	if (var == NULL)
		return false;

	auto splits = Utils::strsplit(var, ":");
	auto newEnv = std::string();

	for(unsigned int i = 0; i < splits.size(); i++)
	{
		auto split = splits.at(i);
		if (split.ends_with("SLSsteam.so"))
		{
			Utils::log("Removed SLSsteam.so from $%s\n", varName);
			continue;
		}

		if(newEnv.size() > 0)
		{
			newEnv.append(":");
		}
		newEnv.append(split);
	}

	setenv(varName, newEnv.c_str(), 1);
	//Utils::log("Set %s to %s\n", varName, newEnv.c_str());

	return true;
}

static pthread_t SLSsteam_mainThread;

void* SLSsteam_init(void*)
{
	lm_process_t proc {};
	if (!LM_GetProcess(&proc))
	{
		exit(1);
	}

	//Do not do anything in other processes
	if (strcmp(proc.name, "steam") != 0)
	{
		return nullptr;
	}

	Utils::init();
	Utils::log("SLSsteam loaded in %s\n", proc.name);

	removeSLSsteamFromEnvVar("LD_AUDIT");
	removeSLSsteamFromEnvVar("LD_PRELOAD");

	g_config.init();

	//TODO: Replace with Mutex (does not seem possible)
	for(;;)
	{
		if(LM_FindModule("steamclient.so", &g_modSteamClient))
		{
			auto path = std::filesystem::path(g_modSteamClient.path);
			auto dir = path.parent_path();
			Utils::log("steamclient.so loaded from %s/%s at %p\n", dir.filename().c_str(), path.filename().c_str(), g_modSteamClient.base);

			std::ifstream steamclient(path, std::ios::binary);
			if (!steamclient.is_open())
			{
				Utils::warn("Failed to open steamclient.so for hash checking!");
				return nullptr;
			}

			std::vector<unsigned char> steamclientBytes(std::istreambuf_iterator<char>(steamclient), {});
			unsigned char sha256Bytes[SHA256_DIGEST_LENGTH];
			SHA256(steamclientBytes.data(), steamclientBytes.size(), sha256Bytes);

			std::stringstream sha256;
			for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
			{
				sha256 << std::hex << std::setw(2) << std::setfill('0') << (int)sha256Bytes[i];
			}
			Utils::log("steamclient.so hash is %s\n", sha256.str().c_str());

			steamclient.close();
			if (strcmp(sha256.str().c_str(), SAFE_HASH) != 0)
			{
				if (g_config.safeMode)
				{
					Utils::warn("Unknown steamclient.so hash! Aborting initialization");
					return nullptr;
				}
				else if(g_config.warnHashMissmatch)
				{
					Utils::warn("Unknown steamclient.so hash!");
				}
			}

			break;
		}

		usleep(1000 * 1000 * 1);
	}

	Utils::notify("SLSsteam placing hooks");

	Hooks::setup();
	return nullptr;
}

__attribute__((constructor)) static void init()
{
	if(pthread_create(&SLSsteam_mainThread, nullptr, SLSsteam_init, nullptr))
	{
		exit(1);
	}
}

unsigned int la_version(unsigned int)
{
	return LAV_CURRENT;
}
