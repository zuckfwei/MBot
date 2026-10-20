#pragma once

#include <string>

class CustomRequestService
{
public:
	enum class Preset
	{
		League,
		Riot,
		Store,
		Ledge
	};

	struct Connection
	{
		std::string url;
		std::string port;
		std::string header;
	};

	static bool GetConnection(Preset preset, Connection& connection, std::string& error);

	static std::string Send(const std::string& method, const std::string& url,
		const std::string& body, const std::string& port, const std::string& header,
		std::string& error);

	static std::string Invoke(const std::string& destination, const std::string& method,
		const std::string& args);

private:
	static bool NormalizeUrl(const std::string& url, const std::string& port,
		const std::string& header, std::string& normalizedUrl, std::string& error);
	static std::string GetLedgeUrl();
};
