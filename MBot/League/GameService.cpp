#include "GameService.h"

#include <format>
#include <vector>

#include <cpr/cpr.h>
#include <json/json.h>

#include "JsonUtils.h"
#include "LCU.h"
#include "Utils.h"

namespace
{
	enum class MultiSearchServer
	{
		Unknown,
		BR,
		EUNE,
		EUW,
		LAN,
		LAS,
		NA
	};

	std::string UrlEncode(const std::string& value)
	{
		static constexpr char hex[] = "0123456789ABCDEF";
		std::string encoded;
		for (const unsigned char character : value)
		{
			if ((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
				(character >= '0' && character <= '9') || character == '-' || character == '_' ||
				character == '.' || character == '~')
			{
				encoded += static_cast<char>(character);
			}
			else
			{
				encoded += '%';
				encoded += hex[character >> 4];
				encoded += hex[character & 0x0F];
			}
		}
		return encoded;
	}

	std::string JoinSummonerNames(const std::vector<std::string>& names, const bool encodeComma,
		const bool trailingComma)
	{
		const std::string separator = encodeComma ? "%2C" : ",";
		std::string joined;
		for (size_t index = 0; index < names.size(); index++)
		{
			joined += UrlEncode(names[index]);
			if (trailingComma || index + 1 < names.size())
				joined += separator;
		}
		return joined;
	}

	MultiSearchServer ParseServer(std::string region)
	{
		region = Utils::ToUpper(region);
		if (region == "BR" || region == "BR1")
			return MultiSearchServer::BR;
		if (region == "EUNE" || region == "EUN1")
			return MultiSearchServer::EUNE;
		if (region == "EUW" || region == "EUW1")
			return MultiSearchServer::EUW;
		if (region == "LAN" || region == "LA1")
			return MultiSearchServer::LAN;
		if (region == "LAS" || region == "LA2")
			return MultiSearchServer::LAS;
		if (region == "NA" || region == "NA1")
			return MultiSearchServer::NA;
		return MultiSearchServer::Unknown;
	}

	std::string ToOpggRegion(const MultiSearchServer server)
	{
		switch (server)
		{
		case MultiSearchServer::BR:
			return "br";
		case MultiSearchServer::EUNE:
			return "eune";
		case MultiSearchServer::EUW:
			return "euw";
		case MultiSearchServer::LAN:
			return "lan";
		case MultiSearchServer::LAS:
			return "las";
		case MultiSearchServer::NA:
			return "na";
		default:
			return {};
		}
	}

	std::vector<std::string> GetVisibleTeamNames(const Json::Value& team)
	{
		std::vector<std::string> names;
		for (const auto& player : team)
		{
			const std::string summonerId = player["summonerId"].asString();
			if (summonerId == "0")
				continue;

			Json::Value summoner;
			if (JsonUtils::Parse(LCU::Request("GET", "/lol-summoner/v1/summoners/" + summonerId), summoner))
				names.emplace_back(summoner["gameName"].asString() + "#" + summoner["tagLine"].asString());
		}
		return names;
	}

	std::vector<std::string> GetHiddenTeamNames()
	{
		if (!LCU::SetCurrentClientRiotInfo())
			return {};

		const std::string response = cpr::Get(
			cpr::Url{ std::format("https://127.0.0.1:{}/chat/v5/participants", LCU::riot.port) },
			cpr::Header{ Utils::StringToHeader(LCU::riot.header) }, cpr::VerifySsl{ false }).text;

		Json::Value root;
		if (!JsonUtils::Parse(response, root) || !root["participants"].isArray())
			return {};

		std::vector<std::string> names;
		for (const auto& participant : root["participants"])
		{
			if (participant["cid"].asString().contains("champ-select"))
				names.emplace_back(participant["game_name"].asString() + "#" + participant["game_tag"].asString());
		}
		return names;
	}

	std::string GetRegion(const bool platformId)
	{
		Json::Value root;
		const std::string endpoint = platformId ? "/lol-rso-auth/v1/authorization" : "/riotclient/region-locale";
		if (!JsonUtils::Parse(LCU::Request("GET", endpoint), root))
			return {};
		return root[platformId ? "currentPlatformId" : "webRegion"].asString();
	}
}

std::string GameService::BuildMultiSearchUrl(const std::string& website)
{
	Json::Value champSelect;
	const std::string response = LCU::Request("GET", "/lol-champ-select/v1/session");
	if (response.empty() || response.find("RPC_ERROR") != std::string::npos)
		return "Champion select not found";
	if (!JsonUtils::Parse(response, champSelect))
		return "Failed to parse champion select";

	const Json::Value team = champSelect["myTeam"];
	if (!team.isArray())
		return "Failed to get team";

	bool namesAreHidden = false;
	for (const auto& player : team)
	{
		if (player["nameVisibilityType"].asString() == "HIDDEN")
		{
			namesAreHidden = true;
			break;
		}
	}

	const std::vector<std::string> names = namesAreHidden ? GetHiddenTeamNames() : GetVisibleTeamNames(team);
	if (names.empty())
		return "Failed to get summoner names";

	const bool needsPlatformId = website == "U.GG";
	const std::string region = GetRegion(needsPlatformId);
	if (region.empty())
		return "Failed to get region";

	if (website == "OP.GG")
	{
		const std::string opggRegion = ToOpggRegion(ParseServer(region));
		if (opggRegion.empty())
			return "Unsupported OP.GG region: " + region;
		return "https://op.gg/multisearch/" + opggRegion + "?summoners=" + JoinSummonerNames(names, true, true);
	}
	if (website == "U.GG")
		return "https://u.gg/multisearch?summoners=" + JoinSummonerNames(names, false, false) +
			"&region=" + Utils::ToLower(region);
	if (website == "PORO.GG")
		return "https://poro.gg/multi?region=" + Utils::ToUpper(region) + "&q=" + JoinSummonerNames(names, false, false);
	if (website == "Porofessor.gg")
		return "https://porofessor.gg/pregame/" + region + "/" + JoinSummonerNames(names, false, false) +
			"/soloqueue/season";
	return "Unknown multi-search website";
}
