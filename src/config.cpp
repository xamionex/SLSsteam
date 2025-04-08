#include "config.hpp"
#include "utils.hpp"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

//TODO: Move into own .yaml file somehow
static const char* defaultConfig = 
"#Example AppIds Config for those not familiar with YAML:\n"
"#AppIds:\n"
"# - 440\n"
"# - 730\n"
"#Take care of not messing up your spaces! Otherwise it won't work\n\n"
"#Disables Family Share license locking for self and others\n"
"DisableFamilyShareLock: yes\n\n"
"#Switches to whitelist instead of the default blacklist\n"
"UseWhitelist: no\n\n"
"#Automatically filter Apps in CheckAppOwnership. Filters everything but Games and Applications. Should not affect DLC checks\n"
"#Overrides black-/whitelist. Gets overriden by AdditionalApps\n"
"AutoFilterList: yes\n\n"
"#List of AppIds to ex-/include\n"
"AppIds:\n\n"
"#Enables playing of not owned games. Respects black-/whitelist AppIds\n"
"PlayNotOwnedGames: no\n\n"
"#Additional AppIds to inject (Overrides your black-/whitelist & also overrides OwnerIds for apps you got shared!) Best to use this only on games NOT in your library.\n"
"AdditionalApps:\n\n"
"#Logs all calls to Steamworks (this makes the logfile huge! Only useful for debugging/analyzing\n"
"ExtendedLogging: no";

std::string CConfig::getDir()
{
	char pathBuf[255];
	const char* configDir = getenv("XDG_CONFIG_HOME"); //Most users should have this set iirc
	if (configDir != NULL)
	{
		sprintf(pathBuf, "%s/SLSsteam", configDir);
	} else
	{
		const char* home = getenv("HOME");
		sprintf(pathBuf, "%s/.config/SLSsteam", home);
	}

	return std::string(pathBuf);
}

std::string CConfig::getPath()
{
	return getDir().append("/config.yaml");
}

bool CConfig::init()
{
	std::string path = getPath();
	if (!std::filesystem::exists(path))
	{
		std::string dir = getDir();
		if (!std::filesystem::exists(dir))
		{
			if (!std::filesystem::create_directories(dir))
			{
				Utils::log("Unable to create config directory at %s!\n", dir.c_str());
				loadSettings();
				return false;
			}

			Utils::log("Created config directory at %s\n");
		}

		FILE* file = fopen(path.c_str(), "w");
		if (!file)
		{
			Utils::log("Unable to create config at %s!\n", path.c_str());
			loadSettings();
			return false;
		}

		fputs(defaultConfig, file);
		fflush(file);
		fclose(file);
	}

	loadSettings();
	return true;
}

bool CConfig::loadSettings()
{
	YAML::Node node;
	try
	{
		node = YAML::LoadFile(getPath());
	}
	catch (YAML::BadFile& bf)
	{
		Utils::log("Can not read config.yaml! %s\nUsing defaults\n", bf.msg.c_str());
		node = YAML::Node(); //Create empty node and let defaults kick in
	}
	catch (YAML::ParserException& pe)
	{
		Utils::log("Error parsing config.yaml! %s\nUsing defaults\n", pe.msg.c_str());
		node = YAML::Node(); //Create empty node and let defaults kick in
	}
	
	disableFamilyLock = getSetting<bool>(node, "DisableFamilyShareLock", true);
	useWhiteList = getSetting<bool>(node, "UseWhitelist", false);
	automaticFilter = getSetting<bool>(node, "AutoFilterList", true);
	appIds = getSetting(node, "AppIds", std::vector<uint32_t>());
	playNotOwnedGames = getSetting<bool>(node, "PlayNotOwnedGames", false);
	addedAppIds = getSetting(node, "AdditionalApps", std::vector<uint32_t>());
	extendedLogging = getSetting<bool>(node, "ExtendedLogging", false);

	//TODO: Create smart logging function to log them automatically via getSetting
	Utils::log("DisableFamilyShareLock: %i\n", disableFamilyLock);
	Utils::log("UseWhitelist: %i\n", useWhiteList);
	Utils::log("AutoFilterList: %i\n", automaticFilter);
	Utils::log("AppIds:\n");
	for(auto& appId : appIds)
	{
	 	Utils::log("%u\n", appId);
	}
	Utils::log("PlayNotOwnedGames: %i\n", playNotOwnedGames);
	Utils::log("AdditionalApps:\n");
	for(auto& appId : addedAppIds)
	{
	 	Utils::log("%u\n", appId);
	}
	Utils::log("ExtendedLogging: %i\n", extendedLogging);

	return true;
}
bool CConfig::isAddedAppId(uint32_t appId)
{
	return isAddedAppId(appId, true);
}

bool CConfig::isAddedAppId(uint32_t appId, bool cache)
{
	if (_cachedAddedAppIds.count(appId))
		return _cachedAddedAppIds.at(appId);

	bool found = std::find(addedAppIds.begin(), addedAppIds.end(), appId) != addedAppIds.end();
	if (cache)
	{
		_cachedAddedAppIds[appId] = found;
	}
	return found;
}

bool CConfig::shouldExcludeAppId(uint32_t appId)
{
	if (_cachedAppIds.count(appId))
		return _cachedAppIds.at(appId);

	bool exclude = false;

	//Proper way would be with getAppType, but that seems broken so we need to do this instead
	if (appId >= pow(10, 9)) //Higher and equal to 10^9 gets used by Steam Internally
	{
		exclude = true;
	}
	else
	{
		bool found = std::find(appIds.begin(), appIds.end(), appId) != appIds.end();
		exclude = !isAddedAppId(appId) && ((useWhiteList && !found) || (!useWhiteList && found));
	}

	_cachedAppIds[appId] = exclude;
	Utils::log("shouldExcludeAppId(%u) -> %i\n", appId, exclude);
	return exclude;
}

bool CConfig::addAdditionalAppId(uint32_t appId)
{
	if (isAddedAppId(appId, false))
		return false;

	addedAppIds.emplace_back(appId);
	_cachedAddedAppIds[appId] = true;
	return true;
}

CConfig g_config = CConfig();
