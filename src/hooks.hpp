#pragma once

#include "libmem/libmem.h"

#include <vector>

namespace Hooks
{
	typedef void(*LogSteamPipeCall_t)(const char*, const char*);
	typedef bool(*CheckAppOwnership_t)(void*, uint32_t, void*);
	typedef void(*IClientAppManager_PipeLoop_t)(void*, void*, void*, void*);
	typedef void(*IClientApps_PipeLoop_t)(void*, void*, void*, void*);
	typedef bool(*IClientUser_BIsSubscribedApp_t)(void*, uint32_t);
	typedef uint32_t(*IClientUser_GetSubscribedApps_t)(void*, uint32_t*, size_t, bool);
	typedef void(*IClientUser_PipeLoop_t)(void*, void*, void*, void*);

	extern LogSteamPipeCall_t LogSteamPipeCall;
	extern CheckAppOwnership_t CheckAppOwnership;
	extern IClientAppManager_PipeLoop_t IClientAppManager_PipeLoop;
	extern IClientApps_PipeLoop_t IClientApps_PipeLoop;
	extern IClientUser_BIsSubscribedApp_t IClientUser_BIsSubscribedApp;
	extern IClientUser_GetSubscribedApps_t IClientUser_GetSubscribedApps;
	extern IClientUser_PipeLoop_t IClientUser_PipeLoop;

	//TODO: Split setup into actual setup and hooking functions
	bool setup();
	bool checkAddresses(std::vector<lm_address_t>);
}
