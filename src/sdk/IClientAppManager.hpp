#pragma once

#include <cstdint>

enum EAppState : int
{
	None = 1, //Default ?
	Downloading = 2,
	Installed = 4,
	Installing = 512
};

class IClientAppManager
{
public:
	bool installApp(uint32_t appId);
	EAppState getAppInstallState(uint32_t appId);
};

extern IClientAppManager* g_pClientAppManager;
