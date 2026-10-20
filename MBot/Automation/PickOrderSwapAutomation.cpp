#include "PickOrderSwapAutomation.h"

#include <Windows.h>

#include <format>

#include "Config.h"
#include "Definitions.h"
#include "JsonUtils.h"
#include "Utils.h"
#include "LcuHttpClient.h"

namespace
{
	bool IsSuccessful(const cpr::Response& response)
	{
		return response.status_code >= 200 && response.status_code < 300;
	}
}

GameAutomation::PickOrderSwapAutomation::PickOrderSwapAutomation(League::LcuHttpClient& http) : http_(http)
{
}

void GameAutomation::PickOrderSwapAutomation::Reset()
{
	queueEligibility_ = -1;
	requested_ = false;
	lastAttempt_ = {};
}

void GameAutomation::PickOrderSwapAutomation::EnsureQueueEligibility()
{
	if (queueEligibility_ >= 0)
		return;

	const cpr::Response response = http_.Request("GET", "/lol-gameflow/v1/session");
	Json::Value gameflow;
	if (!IsSuccessful(response) || !JsonUtils::Parse(response.text, gameflow))
		return;

	const Json::Value& queue = gameflow["gameData"]["queue"];
	if (!queue.isObject() || !queue["id"].isNumeric() || !queue["mapId"].isNumeric() ||
		!queue["numPlayersPerTeam"].isNumeric())
	{
		return;
	}

	const int queueId = queue["id"].asInt();
	const bool supportedQueue = queueId == QueueID::DraftPick || queueId == QueueID::SoloDuo ||
		queueId == QueueID::Flex;
	queueEligibility_ = supportedQueue && queue["mapId"].asInt() == 11 &&
		queue["numPlayersPerTeam"].asInt() == 5 ? 1 : 0;
}

int GameAutomation::PickOrderSwapAutomation::GetFirstTeamPickCellId(const Json::Value& champSelect)
{
	const Json::Value& myTeam = champSelect["myTeam"];
	const Json::Value& actionGroups = champSelect["actions"];
	if (!myTeam.isArray() || myTeam.size() != 5 || !actionGroups.isArray())
		return -1;

	for (const auto& actionGroup : actionGroups)
	{
		if (!actionGroup.isArray())
			continue;

		int firstCellId = -1;
		for (const auto& action : actionGroup)
		{
			if (action["type"].asString() != "pick")
				continue;

			const int actorCellId = action["actorCellId"].asInt();
			for (const auto& player : myTeam)
			{
				if (player["cellId"].asInt() != actorCellId)
					continue;
				if (firstCellId < 0 || actorCellId < firstCellId)
					firstCellId = actorCellId;
				break;
			}
		}

		if (firstCellId >= 0)
			return firstCellId;
	}

	return -1;
}

void GameAutomation::PickOrderSwapAutomation::Handle(const Json::Value& champSelect)
{
	if (!S.gameTab.instaTradeFirstPick || requested_)
		return;

	EnsureQueueEligibility();
	if (queueEligibility_ != 1)
		return;

	const int localCellId = champSelect["localPlayerCellId"].asInt();
	const int targetCellId = GetFirstTeamPickCellId(champSelect);
	if (targetCellId < 0)
		return;
	if (targetCellId == localCellId)
	{
		requested_ = true;
		return;
	}

	int swapId = -1;
	if (const Json::Value& swaps = champSelect["pickOrderSwaps"]; swaps.isArray())
	{
		for (const auto& swap : swaps)
		{
			if (swap["cellId"].asInt() != targetCellId)
				continue;

			const std::string state = Utils::ToUpper(swap["state"].asString());
			if (state == "SENT" || state == "RECEIVED" || state == "ACCEPTED")
			{
				requested_ = true;
				return;
			}
			if (state == "AVAILABLE")
				swapId = swap["id"].asInt();
			break;
		}
	}
	if (swapId < 0)
		return;

	const auto now = std::chrono::steady_clock::now();
	if (lastAttempt_.time_since_epoch().count() != 0 &&
		now - lastAttempt_ < std::chrono::milliseconds(100))
	{
		return;
	}
	lastAttempt_ = now;

	const cpr::Response response = http_.Request("POST", std::format(
		"/lol-champ-select/v1/session/pick-order-swaps/{}/request", swapId));
	Json::Value result;
	if (IsSuccessful(response) && JsonUtils::Parse(response.text, result) &&
		Utils::ToUpper(result["state"].asString()) == "SENT")
	{
		requested_ = true;
	}
}
