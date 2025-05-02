#pragma once

#include "log.hpp"

#include "yaml-cpp/exceptions.h"
#include "yaml-cpp/node/node.h"

#include <cstdint>
#include <cstdio>
#include <pthread.h>
#include <string>
#include <unordered_set>

class CConfig {
public:
	std::unordered_set<uint32_t> appIds;
	std::unordered_set<uint32_t> addedAppIds;

	bool disableFamilyLock;
	bool useWhiteList;
	bool automaticFilter;
	bool playNotOwnedGames;
	bool safeMode;
	bool warnHashMissmatch;
	bool extendedLogging;

	std::string getDir();
	std::string getPath();
	bool createFile();
	bool init();

	bool loadSettings();
	template<typename T> T getSetting(YAML::Node node, const char* name, T defVal)
	{
		if (!node[name])
		{
			g_pLog->notifyLong("Missing %s in configfile! Using default", name);
			return defVal;
		}

		try
		{
			 return node[name].as<T>();
		}
		catch (YAML::BadConversion& er)
		{
			g_pLog->notify("Failed to parse value of %s! Using default\n", name);
			return defVal;
		}
	};

	bool isAddedAppId(uint32_t appId);
	bool addAdditionalAppId(uint32_t appId);

	bool shouldExcludeAppId(uint32_t appId);
};

extern CConfig g_config;
