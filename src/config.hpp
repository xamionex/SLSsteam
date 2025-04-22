#pragma once

#include "log.hpp"

#include "yaml-cpp/exceptions.h"
#include "yaml-cpp/node/node.h"

#include <cstdint>
#include <cstdio>
#include <pthread.h>
#include <string>
#include <unordered_map>
#include <vector>

class CConfig {
	std::unordered_map<uint32_t, bool> _cachedAppIds;
	//TODO: Replace with unordered_set
	std::unordered_map<uint32_t, bool> _cachedAddedAppIds;

public:
	//Using a map directly would be soooo much better, but for proper logging this is better
	std::vector<uint32_t> appIds;
	std::vector<uint32_t> addedAppIds;

	bool disableFamilyLock;
	bool useWhiteList;
	bool automaticFilter;
	bool playNotOwnedGames;
	bool safeMode;
	bool warnHashMissmatch;
	bool extendedLogging;

	std::string getDir();
	std::string getPath();
	bool init();

	bool loadSettings();
	template<typename T> T getSetting(YAML::Node node, const char* name, T defVal)
	{
		T ret = defVal;

		if (!node[name])
		{
			g_pLog->warn("Missing %s in configfile! Using default", name);
		}
		else
		{
			try
			{
				 ret = node[name].as<T>();
			}
			catch (YAML::BadConversion& er)
			{
				g_pLog->notify("Failed to parse value of {}! Using default\n", name);
			}
		}

		return ret;
	};

	bool isAddedAppId(uint32_t appId);
	bool isAddedAppId(uint32_t appId, bool cache);
	bool shouldExcludeAppId(uint32_t appId);

	bool addAdditionalAppId(uint32_t appId);
};

extern CConfig g_config;
