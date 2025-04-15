#include "config.hpp"
#include "globals.hpp"
#include "hooks.hpp"
#include "utils.hpp"

#include "libmem/libmem.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <link.h>
#include <pthread.h>
#include <cstdio>
#include <string>
#include <unistd.h>

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
			Utils::log("Removed %s from $%s\n", split.c_str(), varName);
			continue;
		}

		if(newEnv.size() > 0)
		{
			newEnv.append(":");
		}
		newEnv.append(split);
	}

	setenv(varName, newEnv.c_str(), 1);
	Utils::log("Set %s to %s\n", varName, newEnv.c_str());

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
	Utils::notify("SLSsteam loading...");

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
			break;
		}

		usleep(1000 * 1000 * 1);
	}

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
