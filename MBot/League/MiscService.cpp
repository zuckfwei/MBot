#include "MiscService.h"

#include <algorithm>
#include <format>
#include <regex>

#include <cpr/cpr.h>
#include <json/json.h>

#include "JsonUtils.h"
#include "LCU.h"
#include "Utils.h"

std::string MiscService::RestartUx()
{
	std::string result = LCU::Request("POST", "/riotclient/kill-and-restart-ux", "");
	if (result.find("failed") != std::string::npos && LCU::SetLeagueClientInfo())
		result = "Rehooked to new league client";
	return result;
}

std::string MiscService::CloseClient()
{
	return LCU::Request("POST", "/process-control/v1/process/quit", "");
}

std::string MiscService::AcceptFriendRequests()
{
	Json::Value root;
	if (!JsonUtils::Parse(LCU::Request("GET", "/lol-chat/v1/friend-requests"), root))
		return "Failed to parse JSON";
	if (!root.isArray())
		return "Friend requests not found";

	int accepted = 0;
	for (const auto& request : root)
	{
		const std::string pid = request["pid"].asString();
		if (pid.empty())
			continue;

		LCU::Request("PUT", "/lol-chat/v1/friend-requests/" + pid, R"({"direction":"both"})");
		accepted++;
	}
	return "Accepted " + std::to_string(accepted) + " friend requests";
}

std::string MiscService::DeleteFriendRequests()
{
	Json::Value root;
	if (!JsonUtils::Parse(LCU::Request("GET", "/lol-chat/v1/friend-requests"), root))
		return "Failed to parse JSON";
	if (!root.isArray())
		return "Friend requests not found";

	int deleted = 0;
	for (const auto& request : root)
	{
		const std::string pid = request["pid"].asString();
		if (pid.empty())
			continue;

		LCU::Request("DELETE", "/lol-chat/v1/friend-requests/" + pid, "");
		deleted++;
	}
	return "Deleted " + std::to_string(deleted) + " friend requests";
}

std::vector<MiscService::FriendGroup> MiscService::GetFriendGroups()
{
	Json::Value root;
	if (!JsonUtils::Parse(LCU::Request("GET", "/lol-chat/v1/friend-groups"), root) || !root.isArray())
		return {};

	std::vector<FriendGroup> groups;
	for (const auto& group : root)
		groups.emplace_back(group["name"].asString(), group["id"].asInt());

	std::ranges::sort(groups, [](const FriendGroup& a, const FriendGroup& b) { return a.id < b.id; });
	return groups;
}

std::string MiscService::RemoveFriendsFromGroup(const int groupId)
{
	Json::Value root;
	if (!JsonUtils::Parse(LCU::Request("GET", "/lol-chat/v1/friends"), root))
		return "Failed to parse JSON";
	if (!root.isArray())
		return "Friends not found";

	int deleted = 0;
	for (const auto& friendEntry : root)
	{
		if (friendEntry["groupId"].asInt() != groupId)
			continue;

		const std::string pid = friendEntry["pid"].asString();
		if (pid.empty())
			continue;

		LCU::Request("DELETE", "/lol-chat/v1/friends/" + pid, "");
		deleted++;
	}
	return "Deleted " + std::to_string(deleted) + " friends";
}

std::string MiscService::SetMinimapScale(const int minimapScale)
{
	return LCU::Request("PATCH", "/lol-game-settings/v1/game-settings",
		std::format(R"({{"HUD":{{"MinimapScale":{:.2f}}}}})", minimapScale / 33.33f));
}

std::string MiscService::DisenchantAll(const std::string& lootType, const std::string& lootLabel)
{
	Json::Value root;
	if (!JsonUtils::Parse(LCU::Request("GET", "/lol-loot/v1/player-loot-map", ""), root) || !root.isObject())
		return "Loot not found";

	int disenchanted = 0;
	const std::regex lootMatcher("^" + lootType + "_[\\d]+");
	for (const std::string& name : root.getMemberNames())
	{
		if (!std::regex_match(name, lootMatcher))
			continue;

		const std::string disenchantCase = lootType == "STATSTONE_SHARD" ? "DISENCHANT" : "disenchant";
		const std::string disenchantName = root[name]["type"].asString();
		LCU::Request("POST", std::format("/lol-loot/v1/recipes/{0}_{1}/craft?repeat=1", disenchantName, disenchantCase),
			std::format(R"(["{}"])", name));
		disenchanted++;
	}
	return std::format("Disenchanted {} {}", disenchanted, lootLabel);
}

std::string MiscService::RefundLastPurchase()
{
	const cpr::Header storeHeader = Utils::StringToHeader(LCU::GetStoreHeader());

	std::string storeUrl = LCU::Request("GET", "/lol-store/v1/getStoreUrl");
	std::erase(storeUrl, '"');

	const std::string purchaseHistory = cpr::Get(cpr::Url{ storeUrl + "/storefront/v3/history/purchase" }, cpr::Header{ storeHeader }).text;

	Json::Value rootPurchaseHistory;
	if (!JsonUtils::Parse(purchaseHistory, rootPurchaseHistory))
		return purchaseHistory;

	const std::string accountId = rootPurchaseHistory["player"]["accountId"].asString();
	const std::string transactionId = rootPurchaseHistory["transactions"][0]["transactionId"].asString();
	return cpr::Post(cpr::Url{ storeUrl + "/storefront/v3/refund" }, cpr::Header{ storeHeader },
		cpr::Body{
			"{\"accountId\":" + accountId + R"(,"transactionId":")" + transactionId +
			R"(","inventoryType":"CHAMPION","language":"en_US"})"
		}).text;
}
