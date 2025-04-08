#include "config.hpp"
#include "globals.hpp"
#include "hooks.hpp"
#include "patterns.hpp"
#include "sdk/IClientApps.hpp"
#include "utils.hpp"
#include "vftableinfo.hpp"

#include "libmem/libmem.h"

#include "sdk/CAppOwnershipInfo.hpp"
#include "sdk/CSteamID.hpp"
#include "sdk/IClientUser.hpp"
#include "sdk/IClientAppManager.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <strings.h>
#include <unistd.h>
#include <vector>

static void(*LogSteamPipeCall)(const char*, const char*);

[[gnu::hot]]
static void hkLogSteamPipeCall(const char* iface, const char* fn)
{
	LogSteamPipeCall(iface, fn);

	if (g_config.extendedLogging)
	{
		Utils::log("LogSteamPipeCall(%s, %s)\n", iface, fn);
	}
}

static bool(*CheckAppOwnership)(void*, uint32_t, void*);
static bool applistRequested = false;
static auto appIdOwnerOverride = std::map<uint32_t, int>();

[[gnu::hot]]
static bool hkCheckAppOwnership(void* a0, uint32_t appId, CAppOwnershipInfo* pOwnershipInfo)
{
	const bool ret = CheckAppOwnership(a0, appId, pOwnershipInfo);
	//Logging disabled, cause it's only useful for debugging and happens a lot
	//Utils::log("CheckAppOwnership(%p, %u, %p) -> %i\n", a0, appId, pOwnershipInfo, ret);
	
	//Wait Until GetSubscribedApps gets called once to let Steam request and populate legit data first.
	//Afterwards modifying should hopefully not affect false positives anymore
	if (!applistRequested || g_config.shouldExcludeAppId(appId) || !pOwnershipInfo || !g_currentUserAccountId)
	{
		return ret;
	}

	if (g_config.isAddedAppId(appId) || (g_config.playNotOwnedGames && !pOwnershipInfo->purchased))
	{
		//Changing the purchased field is enough, but just for nicety in the Steamclient UI we change the owner too
		pOwnershipInfo->ownerSteamId = g_currentUserAccountId;
		pOwnershipInfo->purchased = true;

		//Unnessecary but whatever
		pOwnershipInfo->permanent = true;
		pOwnershipInfo->familyShared = false;

		//Found in backtrace
		pOwnershipInfo->releaseState = 4;
		pOwnershipInfo->field10_0x25 = 0;
		//Seems to do nothing in particular, some dlc have this as 1 so I uncomented this for now. Might be free stuff?
		//pOwnershipInfo->field27_0x36 = 1;

		if(g_config.addAdditionalAppId(appId))
		{
			Utils::log("Force owned %u\n", appId);
		}
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
	if (type == EAppType::DLC) //Don't touch DLC here, otherwise downloads might break. Hopefully this won't decrease compatibility
	{
		return ret;
	}

	if (g_config.automaticFilter)
	{
		switch(type)
		{
			case EAppType::Application:
			case EAppType::Game:
				break;

			default:
				return ret;
		}
	}

	return true;
}

static lm_vmt_t IClientAppManager_vmt;

static void* hkClientAppManager_LaunchApp(void* pClientAppManager, uint32_t* pAppId, void* a2, void* a3, void* a4)
{
	const static auto o = reinterpret_cast<void*(*)(void*, uint32_t*, void*, void*, void*)>
	(
		LM_VmtGetOriginal(&IClientAppManager_vmt, VFTIndexes::IClientAppManager::LaunchApp)
	);

	if (pAppId)
	{
		Utils::log("IClientAppManager::LaunchApp(%p, %u, %p, %p, %p)\n", pClientAppManager, *pAppId, a2, a3, a4);
		appIdOwnerOverride[*pAppId] = 0;
	}

	//Do not do anything in post! Otherwise App launching will break
	return o(pClientAppManager, pAppId, a2, a3, a4);
}

static bool hkClientAppManager_IsAppDlcInstalled(void* pClientAppManager, uint32_t appId, uint32_t dlcId)
{
	const static auto o = reinterpret_cast<bool(*)(void*, uint32_t, uint32_t)>
	(
		LM_VmtGetOriginal(&IClientAppManager_vmt, VFTIndexes::IClientAppManager::IsAppDlcInstalled)
	);

	const bool ret = o(pClientAppManager, appId, dlcId);
	Utils::log("IClientAppManager::IsAppDlcInstalled(%p, %u, %u) -> %i\n", pClientAppManager, appId, dlcId, ret);

	//Do not pretend things are installed while downloading Apps, otherwise downloads will break for some of them
	auto state = g_pClientAppManager->getAppInstallState(appId);
	if (state & EAppState::Downloading || state & EAppState::Installing)
	{
		Utils::log("Skipping DlcId %u because AppId %u has AppState %i\n", dlcId, appId, state);
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
	const static auto o = reinterpret_cast<bool(*)(void*, uint32_t, uint32_t, void*)>
	(
		LM_VmtGetOriginal(&IClientAppManager_vmt, VFTIndexes::IClientAppManager::BIsDlcEnabled)
	);

	const bool ret = o(pClientAppManager, appId, dlcId, a3);
	Utils::log("IClientAppManager::BIsDlcEnabled(%p, %u, %u, %p) -> %i\n", pClientAppManager, appId, dlcId, a3, ret);

	if (g_config.shouldExcludeAppId(dlcId))
	{
		return ret;
	}

	return true;
}

static void(*IClientAppManager_PipeLoop)(void*, void*, void*, void*);

[[gnu::hot]]
static void hkClientAppManager_PipeLoop(void* pClientAppManager, void* a1, void* a2, void* a3)
{
	static bool hooked = false;
	if (!hooked)
	{
		g_pClientAppManager = reinterpret_cast<IClientAppManager*>(pClientAppManager);

		LM_VmtNew(*reinterpret_cast<lm_address_t**>(pClientAppManager), &IClientAppManager_vmt);

		LM_VmtHook(&IClientAppManager_vmt, VFTIndexes::IClientAppManager::LaunchApp, reinterpret_cast<lm_address_t>(&hkClientAppManager_LaunchApp));
		LM_VmtHook(&IClientAppManager_vmt, VFTIndexes::IClientAppManager::IsAppDlcInstalled, reinterpret_cast<lm_address_t>(&hkClientAppManager_IsAppDlcInstalled));
		LM_VmtHook(&IClientAppManager_vmt, VFTIndexes::IClientAppManager::BIsDlcEnabled, reinterpret_cast<lm_address_t>(&hkClientAppManager_BIsDlcEnabled));
		Utils::log("IClientAppManager->vft at %p\n", IClientAppManager_vmt.vtable);
		hooked = true;
	}

	IClientAppManager_PipeLoop(pClientAppManager, a1, a2, a3);
}

static lm_vmt_t IClientApps_vmt;

//Unsure if pDlcId is really what I think it is as I don't have anymore games installed to test it (sorry, needed the disk space lmao)
static bool hkGetDLCDataByIndex(void* pClientApps, uint32_t appId, int dlcIndex, uint32_t* pDlcId, bool* pIsAvailable, const char* dlcName, size_t dlcNameLen)
{
	const static auto o = reinterpret_cast<bool(*)(void*, uint32_t, int, void*, bool*, const char*, size_t)>
	(
		LM_VmtGetOriginal(&IClientApps_vmt, VFTIndexes::IClientApps::GetDLCDataByIndex)
	);

	const bool ret = o(pClientApps, appId, dlcIndex, pDlcId, pIsAvailable, dlcName, dlcNameLen);
	Utils::log("IClientApps::GetDLCDataByIndex(%p, %u, %i, %p, %p, %s, %i) -> %i\n", pClientApps, appId, dlcIndex, pDlcId, pIsAvailable, dlcName, dlcNameLen, ret);

	if (pIsAvailable && pDlcId && !g_config.shouldExcludeAppId(*pDlcId))
	{
		*pIsAvailable = true;
	}

	return ret;
}

static void(*IClientApps_PipeLoop)(void*, void*, void*, void*);

[[gnu::hot]]
static void hkClientApps_PipeLoop(void* pClientApps, void* a1, void* a2, void* a3)
{
	static bool hooked = false;
	if (!hooked)
	{
		g_pClientApps = reinterpret_cast<IClientApps*>(pClientApps);

		LM_VmtNew(*reinterpret_cast<lm_address_t**>(pClientApps), &IClientApps_vmt);

		LM_VmtHook(&IClientApps_vmt, VFTIndexes::IClientApps::GetDLCDataByIndex, reinterpret_cast<lm_address_t>(&hkGetDLCDataByIndex));
		Utils::log("IClientApps->vft at %p\n", IClientApps_vmt.vtable);
		hooked = true;
	}
	return IClientApps_PipeLoop(pClientApps, a1, a2, a3);
}

static lm_vmt_t IClientUser_vmt;

[[gnu::hot]]
static CSteamId* hkClientUser_GetSteamId(void* pClientUser, void* a1)
{
	const static auto o = reinterpret_cast<CSteamId*(*)(void*, void*)>(LM_VmtGetOriginal(&IClientUser_vmt, VFTIndexes::IClientUser::GetSteamID));
	CSteamId* id = o(pClientUser, a1);

	if (!g_currentUserAccountId && id && id->steamId)
	{
		g_currentUserAccountId = id->steamId;
		Utils::log("AcccountId grabbed!\n");
	}

	return id;
}

static bool hkClientUser_BIsSubscribedApp(void* pClientUser, uint32_t appId)
{
	const static auto o = reinterpret_cast<bool(*)(void*, uint32_t)>(LM_VmtGetOriginal(&IClientUser_vmt, VFTIndexes::IClientUser::BIsSubscribedApp));
	const bool ret = o(pClientUser, appId);

	Utils::log("IClientUser::BIsSubscribedApp(%p, %u) -> %i\n", pClientUser, appId, ret);

	if (g_config.shouldExcludeAppId(appId))
	{
		return ret;
	}

	return true;
}

static uint32_t (*IClientUser_GetSubscribedApps)(void*, uint32_t*, size_t, bool);

static uint32_t hkClientUser_GetSubscribedApps(void* pClientUser, uint32_t* pAppList, size_t size, bool a3)
{
	uint32_t count = IClientUser_GetSubscribedApps(pClientUser, pAppList, size, a3);
	Utils::log("IClientUser::GetSubscribedApps(%p, %p, %i, %i) -> %i\n", pClientUser, pAppList, size, a3, count);

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

static void(*IClientUser_PipeLoop)(void*, void*, void*, void*);

[[gnu::hot]]
static void hkClientUser_PipeLoop(void* pClientUser, void* a1, void* a2, void* a3)
{
	//Utils::log("IClientUser::PipeLoop(%p, %p, %p, %p)\n", pClientUser, a1, a2, a3);
	static bool hooked = false;
	if (!hooked)
	{
		g_pSteamUser = reinterpret_cast<IClientUser*>(pClientUser);

		LM_VmtNew(*reinterpret_cast<lm_address_t**>(pClientUser), &IClientUser_vmt);

		LM_VmtHook(&IClientUser_vmt, VFTIndexes::IClientUser::GetSteamID, reinterpret_cast<lm_address_t>(&hkClientUser_GetSteamId));
		LM_VmtHook(&IClientUser_vmt, VFTIndexes::IClientUser::BIsSubscribedApp, reinterpret_cast<lm_address_t>(&hkClientUser_BIsSubscribedApp));

		Utils::log("IClientUser->vft at %p\n", IClientUser_vmt.vtable);
		hooked = true;
	}

	IClientUser_PipeLoop(pClientUser, a1, a2, a3);
}

static void patchRetn(lm_address_t address)
{
	constexpr lm_byte_t retn = 0xC3;

	lm_prot_t oldProt;
	LM_ProtMemory(address, 1, LM_PROT_XRW, &oldProt); //LM_PROT_W Should be enough, but just in case something tries to execute it inbetween use setting the prot and writing to it
	LM_WriteMemory(address, &retn, 1);
	LM_ProtMemory(address, 1, oldProt, LM_NULL);
}

void Hooks::setup()
{
	Utils::log("Hooks::setup()\n");

	lm_address_t logSteamPipeCall = Utils::searchSignature("LogSteamPipeCall", Patterns::LogSteamPipeCall, g_modSteamClient, true);
	lm_address_t checkAppOwnership = Utils::searchSignature("CheckAppOwnership", Patterns::CheckAppOwnership, g_modSteamClient, true);
	lm_address_t runningApp = Utils::searchSignature("RunningApp", Patterns::FamilyGroupRunningApp, g_modSteamClient, true);
	//lm_address_t sharedLibraryLockChanged = Utils::searchSignature("SharedLibraryLockChanged", Patterns::SharedLibraryLockChanged, g_modSteamClient, true);
	//lm_address_t sharedLibraryLockStatus = Utils::searchSignature("SharedLibraryLockStatus", Patterns::SharedLibraryLockStatus, g_modSteamClient);
	lm_address_t stopPlayingBorrowedApp = Utils::searchSignature("StopPlayingBorrowedApp", Patterns::StopPlayingBorrowedApp, g_modSteamClient);

	lm_address_t clientApps_PipeLoop = Utils::searchSignature("IClientApps::PipeLoop", Patterns::IClientApps_PipeLoop, g_modSteamClient, true);
	lm_address_t clientAppManager_PipeLoop = Utils::searchSignature("IClientAppManager::PipeLoop", Patterns::IClientAppManager_PipeLoop, g_modSteamClient, true);
	lm_address_t clientUser_PipeLoop = Utils::searchSignature("IClientUser::PipeLoop", Patterns::IClientUser_PipeLoop, g_modSteamClient, true);

	lm_address_t getSubscribedApps = Utils::searchSignature("IClientUser::GetSubsribedApps", Patterns::GetSubscribedApps, g_modSteamClient, true);

	//TODO: Improve logging further, in case user encounters error I can't replicate
	if (!checkAddresses
		({
			logSteamPipeCall,
			checkAppOwnership,
			runningApp,
			//sharedLibraryLockChanged,
			//sharedLibraryLockStatus,
			stopPlayingBorrowedApp,
			clientApps_PipeLoop,
			clientAppManager_PipeLoop,
			clientUser_PipeLoop,
			getSubscribedApps
		}))
	{
		Utils::log("Not all patterns found! Aborting...");
		return;
	}

	//Disabled by default because it gets called a lot
	LM_HookCode(logSteamPipeCall, reinterpret_cast<lm_address_t>(hkLogSteamPipeCall), reinterpret_cast<lm_address_t*>(&LogSteamPipeCall));
	{
		//TODO: Write wrapper hooking function that automatically does this. Also disable hardcode of instruction and instead follow chunk fn call

		//Replace the PIC thunk call
		char instr_str[30];
		sprintf(instr_str, "mov ebx,%p", reinterpret_cast<void*>(logSteamPipeCall + 9));

		lm_inst_t inst;
		LM_Assemble(instr_str, &inst);

		LM_WriteMemory(reinterpret_cast<lm_address_t>(LogSteamPipeCall) + 4, inst.bytes, inst.size);
	}

	LM_HookCode(checkAppOwnership, reinterpret_cast<lm_address_t>(hkCheckAppOwnership), reinterpret_cast<lm_address_t*>(&CheckAppOwnership));
	{
		//Replace the PIC thunk call
		char instr_str[30];
		sprintf(instr_str, "mov eax,%p", reinterpret_cast<void*>(checkAppOwnership + 5));

		lm_inst_t inst;
		LM_Assemble(instr_str, &inst);

		LM_WriteMemory(reinterpret_cast<lm_address_t>(CheckAppOwnership), inst.bytes, inst.size);
	}

	if (g_config.disableFamilyLock)
	{
		patchRetn(runningApp);
		//patchRetn(sharedLibraryLockChanged);
		//LockStatus calls LockChanged, still noping both because LockChanged gets called by another function and maybe dynamically
		//patchRetn(sharedLibraryLockStatus);
		patchRetn(stopPlayingBorrowedApp);
	}

	LM_HookCode(clientApps_PipeLoop, reinterpret_cast<lm_address_t>(hkClientApps_PipeLoop), reinterpret_cast<lm_address_t*>(&IClientApps_PipeLoop));
	LM_HookCode(clientAppManager_PipeLoop, reinterpret_cast<lm_address_t>(hkClientAppManager_PipeLoop), reinterpret_cast<lm_address_t*>(&IClientAppManager_PipeLoop));

	LM_HookCode(clientUser_PipeLoop, reinterpret_cast<lm_address_t>(hkClientUser_PipeLoop), reinterpret_cast<lm_address_t*>(&IClientUser_PipeLoop));
	LM_HookCode(getSubscribedApps, reinterpret_cast<lm_address_t>(hkClientUser_GetSubscribedApps), reinterpret_cast<lm_address_t*>(&IClientUser_GetSubscribedApps));
	{
		//Replace the PIC thunk call
		char instr_str[30];
		sprintf(instr_str, "mov ecx,%p", reinterpret_cast<void*>(getSubscribedApps + 5));

		lm_inst_t inst;
		LM_Assemble(instr_str, &inst);

		LM_WriteMemory(reinterpret_cast<lm_address_t>(IClientUser_GetSubscribedApps), inst.bytes, inst.size);
	}
}

bool Hooks::checkAddresses(std::vector<lm_address_t> addresses)
{
	for(auto& address : addresses)
	{
		if (address == LM_ADDRESS_BAD)
			return false;
	}

	return true;
}
