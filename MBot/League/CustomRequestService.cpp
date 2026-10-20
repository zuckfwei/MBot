#include "CustomRequestService.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <format>
#include <memory>
#include <ranges>
#include <string_view>

#include <cpr/cpr.h>
#include <json/json.h>

#include "Config.h"
#include "LCU.h"
#include "Utils.h"

namespace
{
	constexpr std::string_view LocalhostUrl = "https://127.0.0.1";

	std::string Trim(std::string value)
	{
		const auto isWhitespace = [](const unsigned char character) { return std::isspace(character) != 0; };
		value.erase(value.begin(), std::ranges::find_if_not(value, isWhitespace));
		value.erase(std::ranges::find_if_not(value.rbegin(), value.rend(), isWhitespace).base(), value.end());
		return value;
	}

	bool ParsePort(const std::string& text, int& port)
	{
		const std::string value = Trim(text);
		if (value.empty())
		{
			port = 443;
			return true;
		}

		const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
		return error == std::errc{} && end == value.data() + value.size() && port > 0 && port <= 65535;
	}

	std::string GetUrlHost(const std::string& url)
	{
		const size_t schemeEnd = url.find("://");
		if (schemeEnd == std::string::npos)
			return {};

		const size_t hostStart = schemeEnd + 3;
		const size_t hostEnd = url.find_first_of("/?#", hostStart);
		return url.substr(hostStart, hostEnd - hostStart);
	}
}

bool CustomRequestService::GetConnection(const Preset preset, Connection& connection, std::string& error)
{
	error.clear();
	connection = {};

	if (preset == Preset::Riot && !LCU::SetCurrentClientRiotInfo())
	{
		error = "Not connected to Riot Client";
		return false;
	}

	if (preset == Preset::League || preset == Preset::Riot)
	{
		const ClientInfo& info = preset == Preset::Riot ? LCU::riot : LCU::league;
		if (info.port <= 0 || info.header.empty())
		{
			error = preset == Preset::Riot ? "Not connected to Riot Client" : "Not connected to League";
			return false;
		}

		connection = { std::string(LocalhostUrl), std::to_string(info.port), info.header };
		return true;
	}

	if (LCU::league.port <= 0 || LCU::league.header.empty())
	{
		error = "Not connected to League";
		return false;
	}

	if (preset == Preset::Store)
	{
		connection.url = LCU::Request("GET", "/lol-store/v1/getStoreUrl");
		std::erase(connection.url, '"');
		connection.url = Trim(connection.url);
		connection.header = LCU::GetStoreHeader();
		connection.port = "443";

		if (connection.url.empty() || connection.header.empty())
		{
			error = "Could not load the Store connection";
			return false;
		}
		return true;
	}

	connection.url = GetLedgeUrl();
	if (connection.url.empty())
	{
		error = "Could not load the Ledge URL";
		return false;
	}

	std::string sessionToken = LCU::Request("GET", "/lol-league-session/v1/league-session-token");
	std::erase(sessionToken, '"');
	sessionToken = Trim(sessionToken);
	if (sessionToken.empty())
	{
		error = "Could not load the League session token";
		return false;
	}

	connection.port = "443";
	connection.header =
		"Host: " + GetUrlHost(connection.url) + "\r\n" +
		"Accept-Encoding: deflate, zstd\r\n" +
		"User-Agent: LeagueOfLegendsClient/" + LCU::league.version + "\r\n" +
		"Authorization: Bearer " + sessionToken + "\r\n" +
		"Content-Type: application/json\r\n" +
		"Accept: application/json\r\n";
	return true;
}

std::string CustomRequestService::Send(const std::string& method, const std::string& url,
	const std::string& body, const std::string& port, const std::string& header,
	std::string& error)
{
	error.clear();

	const std::string upperMethod = Utils::ToUpper(Trim(method));
	if (upperMethod != "GET" && upperMethod != "POST" && upperMethod != "OPTIONS" &&
		upperMethod != "DELETE" && upperMethod != "PUT" && upperMethod != "HEAD" &&
		upperMethod != "PATCH")
	{
		error = upperMethod.empty() ? "Method is empty" : "Unsupported method: " + upperMethod;
		return {};
	}

	std::string normalizedUrl;
	if (!NormalizeUrl(url, port, header, normalizedUrl, error))
		return {};

	cpr::Session session;
	session.SetVerifySsl(false);
	session.SetHeader(Utils::StringToHeader(header));
	session.SetBody(body);
	session.SetUrl(normalizedUrl);

	cpr::Response response;
	if (upperMethod == "GET")
		response = session.Get();
	else if (upperMethod == "POST")
		response = session.Post();
	else if (upperMethod == "OPTIONS")
		response = session.Options();
	else if (upperMethod == "DELETE")
		response = session.Delete();
	else if (upperMethod == "PUT")
		response = session.Put();
	else if (upperMethod == "HEAD")
		response = session.Head();
	else
		response = session.Patch();

	return response.text;
}

std::string CustomRequestService::Invoke(const std::string& destination, const std::string& method,
	const std::string& args)
{
	const std::string request = std::format(
		"/lol-login/v1/session/invoke?destination={}&method={}&args=[{}]",
		destination, method, args);
	return LCU::Request("POST", request, "");
}

bool CustomRequestService::NormalizeUrl(const std::string& url, const std::string& port,
	const std::string& header, std::string& normalizedUrl, std::string& error)
{
	normalizedUrl = Trim(url);
	if (normalizedUrl.empty())
	{
		error = "URL is empty";
		return false;
	}

	int parsedPort = 0;
	if (!ParsePort(port, parsedPort))
	{
		error = "Port must be a number between 1 and 65535";
		return false;
	}

	const size_t schemeEnd = normalizedUrl.find("://");
	if (schemeEnd == std::string::npos)
	{
		if (header.find("Host: 127.0.0.1") == std::string::npos)
		{
			error = "URL needs http:// or https:// when using a custom external header";
			return false;
		}

		if (normalizedUrl.front() != '/')
			normalizedUrl.insert(normalizedUrl.begin(), '/');
		normalizedUrl.insert(0, std::format("https://127.0.0.1:{}", parsedPort));
		return true;
	}

	const std::string scheme = Utils::ToLower(normalizedUrl.substr(0, schemeEnd));
	if (scheme != "http" && scheme != "https")
	{
		error = "URL must use http:// or https://";
		return false;
	}

	const size_t hostStart = schemeEnd + 3;
	const size_t hostEnd = normalizedUrl.find_first_of("/?#", hostStart);
	const size_t authorityEnd = hostEnd == std::string::npos ? normalizedUrl.size() : hostEnd;
	const std::string authority = normalizedUrl.substr(hostStart, authorityEnd - hostStart);
	if (authority.empty())
	{
		error = "URL host is empty";
		return false;
	}

	const bool hasPort = authority.starts_with('[')
		? authority.find("]:") != std::string::npos
		: authority.find(':') != std::string::npos;
	if (!hasPort && parsedPort != 80 && parsedPort != 443)
		normalizedUrl.insert(authorityEnd, ":" + std::to_string(parsedPort));

	return true;
}

std::string CustomRequestService::GetLedgeUrl()
{
	const std::string regionResponse = LCU::Request("GET", "/riotclient/get_region_locale");
	Json::CharReaderBuilder builder;
	const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	JSONCPP_STRING parseError;
	Json::Value root;
	if (!reader->parse(regionResponse.data(), regionResponse.data() + regionResponse.size(), &root, &parseError))
		return {};

	const std::string region = root["webRegion"].asString();
	if (region.empty())
		return {};

	std::ifstream systemYaml(std::filesystem::path(S.leaguePath) / "system.yaml");
	std::string line;
	while (std::getline(systemYaml, line))
	{
		constexpr std::string_view key = "league_edge_url: ";
		const size_t valueStart = line.find(key);
		if (valueStart == std::string::npos || line.find(region) == std::string::npos)
			continue;

		std::string value = Trim(line.substr(valueStart + key.size()));
		if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
			(value.front() == '\'' && value.back() == '\'')))
		{
			value = value.substr(1, value.size() - 2);
		}
		return value;
	}

	return {};
}
