#include "IClientAppManager.hpp"

#include "../utils.hpp"
#include "../vftableinfo.hpp"

#include <cstdint>

bool IClientAppManager::installApp(uint32_t appId)
{
	return Utils::callVFunc<bool(*)(void*, uint32_t, uint32_t, uint32_t)>(VFTIndexes::IClientAppManager::InstallApp, this, appId, 0, 0);
}

EAppState IClientAppManager::getAppInstallState(uint32_t appId)
{
	return Utils::callVFunc<EAppState(*)(void*, uint32_t)>(VFTIndexes::IClientAppManager::GetAppInstallState, this, appId);
}

IClientAppManager* g_pClientAppManager;
