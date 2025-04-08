#pragma once

#include "utils.hpp"

#include "yaml-cpp/exceptions.h"
#include "yaml-cpp/node/node.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <pthread.h>
#include <string>
#include <vector>

class CConfig {
	std::map<uint32_t, bool> _cachedAppIds;
	std::map<uint32_t, bool> _cachedAddedAppIds;

public:
	//Using a map directly would be soooo much better, but for proper logging this is better
	std::vector<uint32_t> appIds;
	std::vector<uint32_t> addedAppIds;

	bool disableFamilyLock;
	bool useWhiteList;
	bool automaticFilter;
	bool playNotOwnedGames;
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
			char msg[255];
			sprintf(msg, "Missing %s in configfile! Using default\n", name);

			//TODO: Do this in a better way, cause this is pretty horrible :)
			//MessageBox* box = new MessageBox("SLSsteam", msg);
			//box->showAsync();

			Utils::log(msg);
		}
		else
		{
			try
			{
				 ret = node[name].as<T>();
			}
			catch (YAML::BadConversion& er)
			{
				Utils::log("Failed to parse value of %s! Using default\n", name);
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
