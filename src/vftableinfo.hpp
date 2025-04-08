#pragma once

namespace VFTIndexes
{
	namespace IClientUser
	{
		constexpr int GetSteamID = 10;
		constexpr int BIsSubscribedApp = 181;
	}
	namespace IClientApps
	{
		constexpr int GetDLCCount = 8;
		constexpr int GetDLCDataByIndex = 9;
		constexpr int GetAppType = 10;
	}
	namespace IClientAppManager
	{
		constexpr int InstallApp = 0;
		constexpr int UninstallApp = 1;
		constexpr int LaunchApp = 2;
		constexpr int GetAppInstallState = 4;
		constexpr int IsAppDlcInstalled = 9;
		constexpr int BIsDlcEnabled = 11;
	}
}
