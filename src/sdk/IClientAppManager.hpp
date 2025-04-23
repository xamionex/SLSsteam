#pragma once

#include <cstdint>

enum EAppState : int
{
	APPSTATE_NONE = 1, //Default ?
	APPSTATE_DOWNLOADING = 2,
	APPSTATE_INSTALLED = 4,
	APPSTATE_INSTALLING = 512
};

class IClientAppManager
{
public:
	bool installApp(uint32_t appId);
	EAppState getAppInstallState(uint32_t appId);
};

extern IClientAppManager* g_pClientAppManager;
