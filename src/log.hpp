#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <openssl/sha.h>
#include <sstream>
#include <unordered_set>

enum LogLevel
{
	Once,
	Debug,
	Info,
	Notify,
	Warn
};

class CLog
{
	std::ofstream ofstream;
	std::unordered_set<const unsigned char*> msgCache;

	constexpr const char* logLvlToStr(LogLevel& lvl)
	{
		switch(lvl)
		{
			case Once:
				return "Once";
			case Debug:
				return "Debug";
			case Info:
				return "Info";
			case Notify:
				return "Notify";
			case Warn:
				return "Warn";

			//Shut gcc warning up
			default:
				return "Unknown";
		}
	}

	template<typename ...Args>
	void __log(LogLevel lvl, const char* msg, Args... args)
	{
		size_t size = snprintf(nullptr, 0, msg, args...) + 1; //Allocate one more byte for zero termination
		char* formatted = reinterpret_cast<char*>(malloc(size));
		snprintf(formatted, size, msg, args...);

		if (lvl == LogLevel::Once)
		{
			unsigned char sha256[SHA256_DIGEST_LENGTH];
			SHA256(reinterpret_cast<const unsigned char*>(formatted), size, sha256);

			if (msgCache.contains(sha256))
			{
				free(formatted);
				return;
			}

			msgCache.emplace(sha256);
		}

		std::stringstream notifySS;

		switch(lvl)
		{
			//TODO: Fix possible breakage when there's only one " in formatted
			case LogLevel::Notify:
				notifySS << "notify-send -u \"normal\" \"" << formatted << "\"";
				break;
			case LogLevel::Warn:
				notifySS << "notify-send -u \"critical\" \"" << formatted << "\"";
				break;

			default:
				break;

		}

		ofstream << "[" << logLvlToStr(lvl) << "] " << formatted;

		if (notifySS.str().size() > 0)
		{
			ofstream << "\n";

			system(notifySS.str().c_str());
			debug("system(\"%s\")\n", notifySS.str().c_str());
		}

		ofstream.flush();
		free(formatted);
	}

public:
	std::string path;

	CLog(const char* path);
	~CLog();

	template<typename ...Args>
	void once(const char* msg, Args... args)
	{
		__log(LogLevel::Once, msg, args...);
	}

	template<typename ...Args>
	void debug(const char* msg, Args... args)
	{
		__log(LogLevel::Debug, msg, args...);
	}

	template<typename ...Args>
	void info(const char* msg, Args... args)
	{
		__log(LogLevel::Info, msg, args...);
	}

	template<typename ...Args>
	void notify(const char* msg, Args... args)
	{
		__log(LogLevel::Notify, msg, args...);
	}

	template<typename ...Args>
	void warn(const char* msg, Args... args)
	{
		__log(LogLevel::Warn, msg, args...);
	}

	static CLog* getDefaultLog();
};

extern std::unique_ptr<CLog> g_pLog;
