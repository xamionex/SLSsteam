#pragma once
#include "memhlp.hpp"

#include "libmem/libmem.h"

#include "sdk/CAppOwnershipInfo.hpp"

#include <cstddef>
#include <memory>
#include <string>

template<typename T>
union FunctionUnion_t
{
	T fn;
	lm_address_t address;
};

//TODO: Look up if there's an interface kinda thing for C++
template<typename T>
class Hook
{
public:
	//TODO: Add base setup fn to set hookFn
	std::string name;
	FunctionUnion_t<T> originalFn;
	FunctionUnion_t<T> hookFn;

	Hook(const char* name);

	virtual void place() = 0;
	virtual void remove() = 0;
};

template<typename T>
class DetourHook : public Hook<T>
{
public:
	FunctionUnion_t<T> tramp;
	size_t size;

	DetourHook(const char* name);

	virtual void place();
	virtual void remove();

	bool setup(const char* pattern, const MemHlp::SigFollowMode followMode, T hookFn);
};

template<typename T>
class VFTHook : public Hook<T>
{
public:
	std::shared_ptr<lm_vmt_t> vft;
	unsigned int index;
	bool hooked;

	VFTHook(const char* name);

	virtual void place();
	virtual void remove();

	void setup(std::shared_ptr<lm_vmt_t> vft, unsigned int index, T hookFn);
};

namespace Hooks
{
	typedef void(*LogSteamPipeCall_t)(const char*, const char*);
	typedef bool(*CheckAppOwnership_t)(void*, uint32_t, CAppOwnershipInfo*);
	typedef void(*IClientAppManager_PipeLoop_t)(void*, void*, void*, void*);
	typedef void(*IClientApps_PipeLoop_t)(void*, void*, void*, void*);
	typedef bool(*IClientUser_BIsSubscribedApp_t)(void*, uint32_t);
	typedef uint32_t(*IClientUser_GetSubscribedApps_t)(void*, uint32_t*, size_t, bool);
	typedef void(*IClientUser_PipeLoop_t)(void*, void*, void*, void*);

	extern DetourHook<LogSteamPipeCall_t> LogSteamPipeCall;
	extern DetourHook<CheckAppOwnership_t> CheckAppOwnership;
	extern DetourHook<IClientAppManager_PipeLoop_t> IClientAppManager_PipeLoop;
	extern DetourHook<IClientApps_PipeLoop_t> IClientApps_PipeLoop;
	extern DetourHook<IClientUser_BIsSubscribedApp_t> IClientUser_BIsSubscribedApp;
	extern DetourHook<IClientUser_GetSubscribedApps_t> IClientUser_GetSubscribedApps;
	extern DetourHook<IClientUser_PipeLoop_t> IClientUser_PipeLoop;

	typedef bool(*IClientAppManager_BIsDlcEnabled_t)(void*, uint32_t, uint32_t, void*);
	typedef void*(*IClientAppManager_LaunchApp_t)(void*, uint32_t*, void*, void*, void*);
	typedef bool(*IClientAppManager_IsAppDlcInstalled_t)(void*, uint32_t, uint32_t);
	typedef bool(*IClientApps_GetDLCDataByIndex_t)(void*, uint32_t, int, uint32_t*, bool*, const char*, size_t);
	typedef void(__attribute((stdcall)) *IClientUser_GetSteamId_t)(void*, void*);

	extern VFTHook<IClientAppManager_BIsDlcEnabled_t> IClientAppManager_BIsDlcEnabled;
	extern VFTHook<IClientAppManager_LaunchApp_t> IClientAppManager_LaunchApp;
	extern VFTHook<IClientAppManager_IsAppDlcInstalled_t> IClientAppManager_IsAppDlcInstalled;
	extern VFTHook<IClientApps_GetDLCDataByIndex_t> IClientApps_GetDLCDataByIndex;
	extern VFTHook<IClientUser_GetSteamId_t> IClientUser_GetSteamId;

	bool setup();
	void place();
	void remove();
}
