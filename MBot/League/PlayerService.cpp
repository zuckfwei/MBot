#include "PlayerService.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <format>
#include <system_error>
#include <utility>

#include <cpr/cpr.h>
#include <json/json.h>

#include "JsonUtils.h"
#include "LCU.h"
#include "Utils.h"

namespace
{
	std::string ToJson(const Json::Value& value)
	{
		Json::StreamWriterBuilder writer;
		writer["indentation"] = "";
		return Json::writeString(writer, value);
	}

	std::string ReadId(const Json::Value& value)
	{
		if (value.isString())
			return value.asString();
		if (value.isUInt64())
			return std::to_string(value.asUInt64());
		if (value.isInt64())
			return std::to_string(value.asInt64());
		return {};
	}

	Json::Value IdValue(const std::string& id)
	{
		Json::UInt64 value = 0;
		const auto [end, error] = std::from_chars(id.data(), id.data() + id.size(), value);
		if (error == std::errc{} && end == id.data() + id.size())
			return value;
		return id;
	}

	League::PlayerService::PlayerInfo ParsePlayer(const std::string& response)
	{
		League::PlayerService::PlayerInfo player;
		player.raw = response;
		player.formatted = JsonUtils::FormatOrRaw(response);

		Json::Value root;
		if (!JsonUtils::Parse(response, root) || !root.isObject())
			return player;

		player.accountId = ReadId(root["accountId"]);
		player.summonerId = ReadId(root["summonerId"]);
		player.puuid = root["puuid"].asString();
		player.internalName = root["internalName"].asString();
		player.gameName = root["gameName"].asString();
		player.tagLine = root["tagLine"].asString();
		player.parsed = !player.summonerId.empty() || !player.puuid.empty();
		return player;
	}

	League::PlayerService::PlayerInfo RequestPlayer(const std::string& endpoint)
	{
		return ParsePlayer(LCU::Request("GET", endpoint));
	}

	std::string SendLobbyInvite(const std::string& summonerId)
	{
		Json::Value invitation;
		invitation["toSummonerId"] = IdValue(summonerId);
		Json::Value invitations(Json::arrayValue);
		invitations.append(std::move(invitation));
		return LCU::Request("POST", "/lol-lobby/v2/lobby/invitations", ToJson(invitations));
	}

	std::string RiotChatRequest(const std::string& endpoint, const Json::Value* body = nullptr)
	{
		if (!LCU::SetCurrentClientRiotInfo())
			return "Not connected to Riot Client";

		cpr::Session session;
		session.SetVerifySsl(false);
		session.SetHeader(Utils::StringToHeader(LCU::riot.header));
		session.SetUrl(std::format("https://127.0.0.1:{}{}", LCU::riot.port, endpoint));
		if (body)
			session.SetBody(ToJson(*body));
		return session.Post().text;
	}
}

League::PlayerService::PlayerInfo League::PlayerService::FindByName(std::string name)
{
	std::erase_if(name, [](const unsigned char character) { return std::isspace(character) != 0; });
	for (size_t position = 0; (position = name.find('#', position)) != std::string::npos; position += 3)
		name.replace(position, 1, "%23");

	return RequestPlayer("/lol-summoner/v1/summoners?name=" + name);
}

League::PlayerService::PlayerInfo League::PlayerService::FindByPuuid(const std::string& puuid)
{
	return RequestPlayer("/lol-summoner/v1/summoners-by-puuid-cached/" + puuid);
}

League::PlayerService::PlayerInfo League::PlayerService::FindBySummonerId(const std::string& summonerId)
{
	return RequestPlayer("/lol-summoner/v1/summoners/" + summonerId);
}

League::PlayerService::PlayerInfo League::PlayerService::GetCurrentPlayer()
{
	Json::Value session;
	const std::string response = LCU::Request("GET", "/lol-login/v1/session");
	if (!JsonUtils::Parse(response, session))
		return ParsePlayer(response);

	return FindBySummonerId(ReadId(session["summonerId"]));
}

std::string League::PlayerService::InviteToLobby(const PlayerInfo& player)
{
	if (!player.parsed || (player.accountId.empty() && player.summonerId.empty()))
		return "Player not found";

	std::string result;
	if (!player.accountId.empty())
		result = SendLobbyInvite(player.accountId);
	if (!player.summonerId.empty())
		result = SendLobbyInvite(player.summonerId);
	return result;
}

std::string League::PlayerService::SendFriendRequest(const PlayerInfo& player)
{
	if (!player.parsed || player.gameName.empty() || player.tagLine.empty())
		return "Player not found";

	Json::Value body;
	body["gameName"] = player.gameName;
	body["tagLine"] = player.tagLine;
	return LCU::Request("POST", "/lol-chat/v2/friend-requests", ToJson(body));
}

std::string League::PlayerService::BlockPlayer(const PlayerInfo& player)
{
	if (!player.parsed || player.summonerId.empty())
		return "Player not found";

	Json::Value body;
	body["summonerId"] = IdValue(player.summonerId);
	return LCU::Request("POST", "/lol-chat/v1/blocked-players", ToJson(body));
}

std::string League::PlayerService::SetStatus(const std::string& status)
{
	Json::Value body;
	body["statusMessage"] = status;
	return LCU::Request("PUT", "/lol-chat/v1/me", ToJson(body));
}

std::string League::PlayerService::SetAvailability(const Availability availability)
{
	Json::Value body;
	switch (availability)
	{
	case Availability::Online:
		body["availability"] = "online";
		break;
	case Availability::Mobile:
		body["availability"] = "mobile";
		break;
	case Availability::Away:
		body["availability"] = "away";
		break;
	case Availability::Offline:
		body["availability"] = "offline";
		break;
	}
	return LCU::Request("PUT", "/lol-chat/v1/me", ToJson(body));
}

std::string League::PlayerService::SuspendChat()
{
	Json::Value body;
	body["config"] = "disable";
	return RiotChatRequest("/chat/v1/suspend", &body);
}

std::string League::PlayerService::ResumeChat()
{
	return RiotChatRequest("/chat/v1/resume");
}

std::string League::PlayerService::SetInvisibleBanner()
{
	const std::string response = LCU::Request("GET", "/lol-challenges/v1/summary-player-data/local-player");
	Json::Value summary;
	if (!JsonUtils::Parse(response, summary) || !summary.isObject())
		return response;

	Json::Value preferences;
	if (summary["topChallenges"].isArray() && !summary["topChallenges"].empty())
	{
		Json::Value challengeIds(Json::arrayValue);
		for (const auto& challenge : summary["topChallenges"])
			challengeIds.append(IdValue(ReadId(challenge["id"])));
		preferences["challengeIds"] = std::move(challengeIds);
	}

	const std::string titleId = ReadId(summary["title"]["itemId"]);
	if (!titleId.empty() && titleId != "-1")
		preferences["title"] = titleId;

	preferences["bannerAccent"] = "2";
	return LCU::Request("POST", "/lol-challenges/v1/update-player-preferences/", ToJson(preferences));
}

std::string League::PlayerService::SetProfileIcon(const int iconId)
{
	Json::Value body;
	body["profileIconId"] = iconId;
	return LCU::Request("PUT", "/lol-summoner/v1/current-summoner/icon", ToJson(body));
}

std::string League::PlayerService::SetChatIcon(const int iconId)
{
	Json::Value body;
	body["icon"] = iconId;
	return LCU::Request("PUT", "/lol-chat/v1/me/", ToJson(body));
}

std::string League::PlayerService::SetBackgroundSkin(const std::string& skinId)
{
	Json::Value body;
	body["key"] = "backgroundSkinId";
	body["value"] = IdValue(skinId);
	return LCU::Request("POST", "/lol-summoner/v1/current-summoner/summoner-profile/", ToJson(body));
}
