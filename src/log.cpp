#include "log.hpp"
#include <memory>

CLog::CLog(const char* path) : path(path)
{
	ofstream = std::ofstream(path, std::ios::out);
	if (!ofstream.is_open())
	{
		throw std::runtime_error("Unable to open logfile!");
	}
}

CLog::~CLog()
{
	if (ofstream.is_open())
	{
		ofstream.close();
	}
}

CLog* CLog::getDefaultLog()
{
	//TODO: Add error checking
	std::stringstream ss;
	ss << getenv("HOME") << "/.SLSsteam.log";

	return new CLog(ss.str().c_str());
}

std::unique_ptr<CLog> g_pLog;
