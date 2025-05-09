#include "config.hpp"
#include "globals.hpp"
#include "hooks.hpp"
#include "log.hpp"
#include "memhlp.hpp"
#include "patterns.hpp"
#include "vftableinfo.hpp"

#include "libmem/libmem.h"

#include "sdk/CAppOwnershipInfo.hpp"
#include "sdk/CSteamID.hpp"
#include "sdk/IClientApps.hpp"
#include "sdk/IClientAppManager.hpp"
#include "sdk/IClientUser.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <strings.h>
#include <unistd.h>

template<typename T>
Hook<T>::Hook(const char* name)
{
	this->name = std::string(name);
}

template<typename T> DetourHook<T>::DetourHook(const char* name) : Hook<T>::Hook(name)
{
	this->size = 0;
}
template<typename T> VFTHook<T>::VFTHook(const char* name) : Hook<T>::Hook(name)
{
	this->hooked = false;
}

template<typename T>
bool DetourHook<T>::setup(const char* pattern, const MemHlp::SigFollowMode followMode, T hookFn)
{
	lm_address_t oFn = MemHlp::searchSignature(this->name.c_str(), pattern, g_modSteamClient, followMode);
	if (oFn == LM_ADDRESS_BAD)
	{
		return false;
	}

	this->originalFn.address = oFn;
	this->hookFn.fn = hookFn;

	return true;
}

template<typename T>
void DetourHook<T>::place()
{
	this->size = LM_HookCode(this->originalFn.address, this->hookFn.address, &this->tramp.address);
	MemHlp::fixPICThunkCall(this->name.c_str(), this->originalFn.address, this->tramp.address);

	g_pLog->debug
	(
		"Detour hooked %s (%p) with hook at %p and tramp at %p\n",
		this->name.c_str(),
		this->originalFn.address,
		this->hookFn.address,
		this->tramp.address
	);
}

template<typename T>
void DetourHook<T>::remove()
{
	if (!this->size)
	{
		return;
	}

	LM_UnhookCode(this->originalFn.address, this->tramp.address, this->size);
	this->size = 0;

	g_pLog->debug("Unhooked %s\n", this->name.c_str());
}

template<typename T>
void VFTHook<T>::place()
{
	LM_VmtHook(this->vft.get(), this->index, this->hookFn.address);
	this->hooked = true;

	g_pLog->debug
	(
		"VFT hooked %s (%p) with hook at %p\n",
		this->name.c_str(),
		this->originalFn.address,
		this->hookFn.address
	);
}

template<typename T>
void VFTHook<T>::remove()
{
	//No clue how libmem reacts when unhooking a non existent hook
	//so we do this
	if (!this->hooked)
	{
		return;
	}

	LM_VmtUnhook(this->vft.get(), this->index);
	this->hooked = false;

	g_pLog->debug("Unhooked %s!\n", this->name.c_str());
}

template<typename T>
void VFTHook<T>::setup(std::shared_ptr<lm_vmt_t> vft, unsigned int index, T hookFn)
{
	this->vft = vft;
	this->index = index;

	this->originalFn.address = LM_VmtGetOriginal(this->vft.get(), this->index);
	this->hookFn.fn = hookFn;
}

__attribute__((hot))
static void hkLogSteamPipeCall(const char* iface, const char* fn)
{
	Hooks::LogSteamPipeCall.tramp.fn(iface, fn);

	if (g_config.extendedLogging)
	{
		g_pLog->debug("LogSteamPipeCall(%s, %s)\n", iface, fn);
	}
}

static bool applistRequested = false;
static auto appIdOwnerOverride = std::map<uint32_t, int>();

__attribute__((hot))
static bool hkCheckAppOwnership(void* a0, uint32_t appId, CAppOwnershipInfo* pOwnershipInfo)
{
	const bool ret = Hooks::CheckAppOwnership.tramp.fn(a0, appId, pOwnershipInfo);

	//Do not log pOwnershipInfo because it gets deleted very quickly, so it's pretty much useless in the logs
	g_pLog->once("CheckAppOwnership(%p, %u) -> %i\n", a0, appId, ret);

	//Wait Until GetSubscribedApps gets called once to let Steam request and populate legit data first.
	//Afterwards modifying should hopefully not affect false positives anymore
	if (!applistRequested || g_config.shouldExcludeAppId(appId) || !pOwnershipInfo || !g_currentSteamId)
	{
		return ret;
	}

	if (g_config.isAddedAppId(appId) || (g_config.playNotOwnedGames && !pOwnershipInfo->purchased))
	{
		//Changing the purchased field is enough, but just for nicety in the Steamclient UI we change the owner too
		pOwnershipInfo->ownerSteamId = g_currentSteamId;
		pOwnershipInfo->purchased = true;

		//Unnessecary but whatever
		pOwnershipInfo->permanent = true;
		pOwnershipInfo->familyShared = false;

		//Found in backtrace
		pOwnershipInfo->releaseState = 4;
		pOwnershipInfo->field10_0x25 = 0;
		//Seems to do nothing in particular, some dlc have this as 1 so I uncomented this for now. Might be free stuff?
		//pOwnershipInfo->field27_0x36 = 1;

		g_config.addAdditionalAppId(appId);
	}

	//Doing that might be not worth it since this will most likely be easier to mantain
	//TODO: Backtrace those 4 calls and only patch the really necessary ones since this might be prone to breakage
	//Also might need to change the ownerSteamId to someone not in the family, since otherwise owned games won't stay unlocked
	if (g_config.disableFamilyLock && appIdOwnerOverride.count(appId) && appIdOwnerOverride.at(appId) < 4)
	{
		pOwnershipInfo->ownerSteamId = 1; //Setting to "arbitrary" steam Id instead of own, otherwise bypass won't work for own games
		//Unnessecarry again, but whatever
		pOwnershipInfo->permanent = true;
		pOwnershipInfo->familyShared = false;

		appIdOwnerOverride[appId]++;
	}

	//Returning false after we modify data shouldn't cause any problems because it should just get discarded

	if (!g_pClientApps)
		return ret;

	auto type = g_pClientApps->getAppType(appId);
	if (type == APPTYPE_DLC) //Don't touch DLC here, otherwise downloads might break. Hopefully this won't decrease compatibility
	{
		return ret;
	}

	if (g_config.automaticFilter)
	{
		switch(type)
		{
			case APPTYPE_APPLICATION:
			case APPTYPE_GAME:
				break;

			default:
				return ret;
		}
	}

	return true;
}

static void* hkClientAppManager_LaunchApp(void* pClientAppManager, uint32_t* pAppId, void* a2, void* a3, void* a4)
{
	if (pAppId)
	{
		g_pLog->once("IClientAppManager::LaunchApp(%p, %u, %p, %p, %p)\n", pClientAppManager, *pAppId, a2, a3, a4);
		appIdOwnerOverride[*pAppId] = 0;
	}

	//Do not do anything in post! Otherwise App launching will break
	return Hooks::IClientAppManager_LaunchApp.originalFn.fn(pClientAppManager, pAppId, a2, a3, a4);
}

static bool hkClientAppManager_IsAppDlcInstalled(void* pClientAppManager, uint32_t appId, uint32_t dlcId)
{
	const bool ret = Hooks::IClientAppManager_IsAppDlcInstalled.originalFn.fn(pClientAppManager, appId, dlcId);
	g_pLog->once("IClientAppManager::IsAppDlcInstalled(%p, %u, %u) -> %i\n", pClientAppManager, appId, dlcId, ret);

	//Do not pretend things are installed while downloading Apps, otherwise downloads will break for some of them
	auto state = g_pClientAppManager->getAppInstallState(appId);
	if (state & APPSTATE_DOWNLOADING || state & APPSTATE_INSTALLING)
	{
		g_pLog->once("Skipping DlcId %u because AppId %u has AppState %i\n", dlcId, appId, state);
		return ret;
	}

	if (g_config.shouldExcludeAppId(dlcId))
	{
		return ret;
	}

	return true;
}

static bool hkClientAppManager_BIsDlcEnabled(void* pClientAppManager, uint32_t appId, uint32_t dlcId, void* a3)
{
	const bool ret = Hooks::IClientAppManager_BIsDlcEnabled.originalFn.fn(pClientAppManager, appId, dlcId, a3);
	g_pLog->once("IClientAppManager::BIsDlcEnabled(%p, %u, %u, %p) -> %i\n", pClientAppManager, appId, dlcId, a3, ret);

	//TODO: Add check for legit ownership to allow toggle on/off
	if (g_config.shouldExcludeAppId(dlcId))
	{
		return ret;
	}

	return true;
}

__attribute__((hot))
static void hkClientAppManager_PipeLoop(void* pClientAppManager, void* a1, void* a2, void* a3)
{
	g_pClientAppManager = reinterpret_cast<IClientAppManager*>(pClientAppManager);

	std::shared_ptr<lm_vmt_t> vft = std::make_shared<lm_vmt_t>();
	LM_VmtNew(*reinterpret_cast<lm_address_t**>(pClientAppManager), vft.get());

	Hooks::IClientAppManager_BIsDlcEnabled.setup(vft, VFTIndexes::IClientAppManager::BIsDlcEnabled, hkClientAppManager_BIsDlcEnabled);
	Hooks::IClientAppManager_LaunchApp.setup(vft, VFTIndexes::IClientAppManager::LaunchApp, hkClientAppManager_LaunchApp);
	Hooks::IClientAppManager_IsAppDlcInstalled.setup(vft, VFTIndexes::IClientAppManager::IsAppDlcInstalled, hkClientAppManager_IsAppDlcInstalled);

	Hooks::IClientAppManager_BIsDlcEnabled.place();
	Hooks::IClientAppManager_LaunchApp.place();
	Hooks::IClientAppManager_IsAppDlcInstalled.place();

	g_pLog->debug("IClientAppManager->vft at %p\n", vft->vtable);

	Hooks::IClientAppManager_PipeLoop.remove();
	Hooks::IClientAppManager_PipeLoop.originalFn.fn(pClientAppManager, a1, a2, a3);
}

//Unsure if pDlcId is really what I think it is as I don't have anymore games installed to test it (sorry, needed the disk space lmao)
static bool hkGetDLCDataByIndex(void* pClientApps, uint32_t appId, int dlcIndex, uint32_t* pDlcId, bool* pIsAvailable, const char* dlcName, size_t dlcNameLen)
{
	const bool ret = Hooks::IClientApps_GetDLCDataByIndex.originalFn.fn(pClientApps, appId, dlcIndex, pDlcId, pIsAvailable, dlcName, dlcNameLen);
	g_pLog->once("IClientApps::GetDLCDataByIndex(%p, %u, %i, %p, %p, %s, %i) -> %i\n", pClientApps, appId, dlcIndex, pDlcId, pIsAvailable, dlcName, dlcNameLen, ret);

	if (pIsAvailable && pDlcId && !g_config.shouldExcludeAppId(*pDlcId))
	{
		*pIsAvailable = true;
	}

	return ret;
}

__attribute__((hot))
static void hkClientApps_PipeLoop(void* pClientApps, void* a1, void* a2, void* a3)
{
	g_pClientApps = reinterpret_cast<IClientApps*>(pClientApps);

	std::shared_ptr<lm_vmt_t> vft = std::make_shared<lm_vmt_t>();
	LM_VmtNew(*reinterpret_cast<lm_address_t**>(pClientApps), vft.get());

	Hooks::IClientApps_GetDLCDataByIndex.setup(vft, VFTIndexes::IClientApps::GetDLCDataByIndex, hkGetDLCDataByIndex);
	Hooks::IClientApps_GetDLCDataByIndex.place();

	g_pLog->debug("IClientApps->vft at %p\n", vft->vtable);

	Hooks::IClientApps_PipeLoop.remove();
	Hooks::IClientApps_PipeLoop.originalFn.fn(pClientApps, a1, a2, a3);
}

//Optimization causes a crash to happen, so we don't
__attribute__((optimize(0), stdcall, hot))
static void hkClientUser_GetSteamId(void* pClientUser, void* a1)
{
	Hooks::IClientUser_GetSteamId.originalFn.fn(pClientUser, a1);

	CSteamId* id = reinterpret_cast<CSteamId*>(pClientUser);
	if (id && id->steamId && !g_currentSteamId)
	{
		g_currentSteamId = id->steamId;
		g_pLog->debug("Grabbed SteamId\n");

		Hooks::IClientUser_GetSteamId.remove();
	}
}

static bool hkClientUser_BIsSubscribedApp(void* pClientUser, uint32_t appId)
{
	const bool ret = Hooks::IClientUser_BIsSubscribedApp.tramp.fn(pClientUser, appId);

	g_pLog->once("IClientUser::BIsSubscribedApp(%p, %u) -> %i\n", pClientUser, appId, ret);

	if (g_config.shouldExcludeAppId(appId))
	{
		return ret;
	}

	return true;
}

static uint32_t hkClientUser_GetSubscribedApps(void* pClientUser, uint32_t* pAppList, size_t size, bool a3)
{
	uint32_t count = Hooks::IClientUser_GetSubscribedApps.tramp.fn(pClientUser, pAppList, size, a3);
	g_pLog->once("IClientUser::GetSubscribedApps(%p, %p, %i, %i) -> %i\n", pClientUser, pAppList, size, a3, count);

	//Valve calls this function twice, once with size of 0 then again
	if (!size || !pAppList)
		return count + g_config.addedAppIds.size();

	//TODO: Maybe Add check if AppId already in list before blindly appending
	for(auto& appId : g_config.addedAppIds)
	{
		pAppList[count++] = appId;
	}

	applistRequested = true;

	return count;
}

__attribute__((hot))
static void hkClientUser_PipeLoop(void* pClientUser, void* a1, void* a2, void* a3)
{
	//Adding a nullcheck should not be necessary. Otherwise Steam messed up beyond recovery anyway
	g_pSteamUser = reinterpret_cast<IClientUser*>(pClientUser);

	std::shared_ptr<lm_vmt_t> vft = std::make_shared<lm_vmt_t>();
	LM_VmtNew(*reinterpret_cast<lm_address_t**>(pClientUser), vft.get());

	Hooks::IClientUser_GetSteamId.setup(vft, VFTIndexes::IClientUser::GetSteamID, hkClientUser_GetSteamId);
	Hooks::IClientUser_GetSteamId.place();

	g_pLog->debug("IClientUser->vft at %p\n", vft->vtable);

	Hooks::IClientUser_PipeLoop.remove();
	Hooks::IClientUser_PipeLoop.originalFn.fn(pClientUser, a1, a2, a3);
}

static void patchRetn(lm_address_t address)
{
	constexpr lm_byte_t retn = 0xC3;

	lm_prot_t oldProt;
	LM_ProtMemory(address, 1, LM_PROT_XRW, &oldProt); //LM_PROT_W Should be enough, but just in case something tries to execute it inbetween use setting the prot and writing to it
	LM_WriteMemory(address, &retn, 1);
	LM_ProtMemory(address, 1, oldProt, LM_NULL);
}

namespace Hooks
{
	DetourHook<LogSteamPipeCall_t> LogSteamPipeCall("LogSteamPipeCall");
	DetourHook<CheckAppOwnership_t> CheckAppOwnership("CheckAppOwnership");
	DetourHook<IClientAppManager_PipeLoop_t> IClientAppManager_PipeLoop("IClientAppManager::PipeLoop");
	DetourHook<IClientApps_PipeLoop_t> IClientApps_PipeLoop("IClientApps::PipeLoop");
	DetourHook<IClientUser_BIsSubscribedApp_t> IClientUser_BIsSubscribedApp("IClientUser::BIsSubscribedApp");
	DetourHook<IClientUser_GetSubscribedApps_t> IClientUser_GetSubscribedApps("IClientUser::GetSubscribedApps");
	DetourHook<IClientUser_PipeLoop_t> IClientUser_PipeLoop("IClientUser::PipeLoop");

	VFTHook<IClientAppManager_BIsDlcEnabled_t> IClientAppManager_BIsDlcEnabled("IClientAppManager::BIsDlcEnabled");
	VFTHook<IClientAppManager_LaunchApp_t> IClientAppManager_LaunchApp("IClientAppManager::LaunchApp");
	VFTHook<IClientAppManager_IsAppDlcInstalled_t> IClientAppManager_IsAppDlcInstalled("IClientAppManager::IsAppDlcInstalled");
	VFTHook<IClientApps_GetDLCDataByIndex_t> IClientApps_GetDLCDataByIndex("IClientApps::GetDLCDataByIndex");
	VFTHook<IClientUser_GetSteamId_t> IClientUser_GetSteamId("IClientUser::GetSteamId");
}

bool Hooks::setup()
{
	g_pLog->debug("Hooks::setup()\n");

	lm_address_t runningApp = MemHlp::searchSignature("RunningApp", Patterns::FamilyGroupRunningApp, g_modSteamClient, MemHlp::SigFollowMode::Relative);
	lm_address_t stopPlayingBorrowedApp = MemHlp::searchSignature("StopPlayingBorrowedApp", Patterns::StopPlayingBorrowedApp, g_modSteamClient, MemHlp::SigFollowMode::PrologueUpwards);

	bool succeeded =
		CheckAppOwnership.setup(Patterns::CheckAppOwnership, MemHlp::SigFollowMode::Relative, &hkCheckAppOwnership)
		&& LogSteamPipeCall.setup(Patterns::LogSteamPipeCall, MemHlp::SigFollowMode::Relative, &hkLogSteamPipeCall)
		&& IClientApps_PipeLoop.setup(Patterns::IClientApps_PipeLoop, MemHlp::SigFollowMode::Relative, &hkClientApps_PipeLoop)
		&& IClientAppManager_PipeLoop.setup(Patterns::IClientAppManager_PipeLoop, MemHlp::SigFollowMode::Relative, &hkClientAppManager_PipeLoop)
		&& IClientUser_PipeLoop.setup(Patterns::IClientUser_PipeLoop, MemHlp::SigFollowMode::Relative, &hkClientUser_PipeLoop)
		&& IClientUser_BIsSubscribedApp.setup(Patterns::IsSubscribedApp, MemHlp::SigFollowMode::Relative, &hkClientUser_BIsSubscribedApp)
		&& IClientUser_GetSubscribedApps.setup(Patterns::GetSubscribedApps, MemHlp::SigFollowMode::Relative, &hkClientUser_GetSubscribedApps)

		&& runningApp != LM_ADDRESS_BAD
		&& stopPlayingBorrowedApp != LM_ADDRESS_BAD;

	if (!succeeded)
	{
		g_pLog->warn("Failed to find all patterns! Aborting...");
		return false;
	}

	if (g_config.disableFamilyLock)
	{
		patchRetn(runningApp);
		patchRetn(stopPlayingBorrowedApp);
	}

	//Might move this into main()
	Hooks::place();
	return true;
}

void Hooks::place()
{
	//Detours
	CheckAppOwnership.place();
	LogSteamPipeCall.place();
	IClientApps_PipeLoop.place();
	IClientAppManager_PipeLoop.place();
	IClientUser_PipeLoop.place();
	IClientUser_BIsSubscribedApp.place();
	IClientUser_GetSubscribedApps.place();
}

void Hooks::remove()
{
	//Detours
	CheckAppOwnership.remove();
	LogSteamPipeCall.remove();
	IClientApps_PipeLoop.remove();
	IClientAppManager_PipeLoop.remove();
	IClientUser_PipeLoop.remove();
	IClientUser_BIsSubscribedApp.remove();
	IClientUser_GetSubscribedApps.remove();

	//VFT Hooks
	IClientAppManager_BIsDlcEnabled.remove();
	IClientAppManager_LaunchApp.remove();
	IClientAppManager_IsAppDlcInstalled.remove();
	IClientApps_GetDLCDataByIndex.remove();
	IClientUser_GetSteamId.remove();
}
