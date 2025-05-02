#include "config.hpp"

#include "yaml-cpp/yaml.h"

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
"#Automatically disable SLSsteam when steamclient.so does not match a predefined file hash that is known to work\n"
"#You should enable this if you're planing to use SLSsteam with Steam Deck's gamemode\n"
"SafeMode: no\n\n"
"#Warn user via notification when steamclient.so hash differs from known safe hash\n"
"#Mostly useful for development so I don't accidentally miss an update\n"
"WarnHashMissmatch: no\n\n"
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

bool CConfig::createFile()
{
	std::string path = getPath();
	if (!std::filesystem::exists(path))
	{
		std::string dir = getDir();
		if (!std::filesystem::exists(dir))
		{
			if (!std::filesystem::create_directories(dir))
			{
				g_pLog->notify("Unable to create config directory at %s!\n", dir.c_str());
				return false;
			}

			g_pLog->debug("Created config directory at %s\n");
		}

		FILE* file = fopen(path.c_str(), "w");
		if (!file)
		{
			g_pLog->notify("Unable to create config at %s!\n", path.c_str());
			return false;
		}

		fputs(defaultConfig, file);
		fflush(file);
		fclose(file);
	}

	return true;
}

bool CConfig::init()
{
	createFile();
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
		g_pLog->notifyLong("Can not read config.yaml! %s\nUsing defaults", bf.msg.c_str());
		node = YAML::Node(); //Create empty node and let defaults kick in
	}
	catch (YAML::ParserException& pe)
	{
		g_pLog->notifyLong("Error parsing config.yaml! %s\nUsing defaults", pe.msg.c_str());
		node = YAML::Node(); //Create empty node and let defaults kick in
	}
	
	disableFamilyLock = getSetting<bool>(node, "DisableFamilyShareLock", true);
	useWhiteList = getSetting<bool>(node, "UseWhitelist", false);
	automaticFilter = getSetting<bool>(node, "AutoFilterList", true);
	//Sadly can't initialize with unordered_set directly since the type is incomplete
	//Same is true for vector too, but an exception has been made for it
	//So we need to convert it later
	const auto appIds = getSetting(node, "AppIds", std::vector<uint32_t>());
	playNotOwnedGames = getSetting<bool>(node, "PlayNotOwnedGames", false);
	const auto addedAppIds = getSetting(node, "AdditionalApps", std::vector<uint32_t>());
	safeMode = getSetting<bool>(node, "SafeMode", false);
	warnHashMissmatch = getSetting<bool>(node, "WarnHashMissmatch", false);
	extendedLogging = getSetting<bool>(node, "ExtendedLogging", false);

	//TODO: Create smart logging function to log them automatically via getSetting
	g_pLog->info("DisableFamilyShareLock: %i\n", disableFamilyLock);
	g_pLog->info("UseWhitelist: %i\n", useWhiteList);
	g_pLog->info("AutoFilterList: %i\n", automaticFilter);
	g_pLog->info("AppIds:\n");
	for(auto& appId : appIds)
	{
		this->appIds.emplace(appId);
		g_pLog->info("%u\n", appId);
	}
	g_pLog->info("PlayNotOwnedGames: %i\n", playNotOwnedGames);
	g_pLog->info("AdditionalApps:\n");
	for(auto& appId : addedAppIds)
	{
		this->addedAppIds.emplace(appId);
		g_pLog->info("%u\n", appId);
	}
	g_pLog->info("SafeMode: %i\n", safeMode);
	g_pLog->info("WarnHashMissmatch: %i\n", warnHashMissmatch);
	g_pLog->info("ExtendedLogging: %i\n", extendedLogging);

	return true;
}

bool CConfig::isAddedAppId(uint32_t appId)
{
	return addedAppIds.contains(appId);
}

bool CConfig::addAdditionalAppId(uint32_t appId)
{
	if (isAddedAppId(appId))
		return false;

	addedAppIds.emplace(appId);
	g_pLog->once("Force owned %u\n", appId); //once is unnessecary but just for consistency
	return true;
}

bool CConfig::shouldExcludeAppId(uint32_t appId)
{
	bool exclude = false;
	//Proper way would be with getAppType, but that seems broken so we need to do this instead
	constexpr uint32_t ONE_BILLION = 1E9; //Implicit cast from double to unsigned int, hopefully this does not break anything
	if (appId >= ONE_BILLION) //Higher and equal to 10^9 gets used by Steam Internally
	{
		exclude = true;
	}
	else
	{
		bool found = appIds.contains(appId);
		exclude = !isAddedAppId(appId) && ((useWhiteList && !found) || (!useWhiteList && found));
	}

	g_pLog->once("shouldExcludeAppId(%u) -> %i\n", appId, exclude);
	return exclude;
}

CConfig g_config = CConfig();
