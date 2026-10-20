#include "ChampSelectAutomation.h"

#include <Windows.h>

#include <chrono>
#include <format>
#include <thread>
#include <vector>

#include "Config.h"
#include "JsonUtils.h"
#include "Utils.h"
#include "ChampionService.h"
#include "LcuHttpClient.h"

namespace
{
	bool IsSuccessful(const cpr::Response& response)
	{
		return response.status_code >= 200 && response.status_code < 300;
	}

	std::string ToJson(const Json::Value& value)
	{
		Json::StreamWriterBuilder builder;
		builder["indentation"] = "";
		return Json::writeString(builder, value);
	}
}

GameAutomation::ChampSelectAutomation::ChampSelectAutomation(League::LcuHttpClient& http) : http_(http)
{
}

void GameAutomation::ChampSelectAutomation::Reset()
{
	useBackupId_ = 0;
	randomPickId_ = 0;
	lastIntentAttempt_ = {};
	instantMuteCells_.clear();
	processedCompletedActions_.clear();
	actionFirstSeen_.clear();
}

void GameAutomation::ChampSelectAutomation::Handle(const Json::Value& champSelect)
{
	ProcessCompletedActions(champSelect);
	TryPickIntent(champSelect);
	TryInstantMute(champSelect);
	TryChampionAction(champSelect);
}

int GameAutomation::ChampSelectAutomation::ResolvePickChampionId(const Json::Value& champSelect)
{
	if (useBackupId_ > 0)
		return useBackupId_;
	if (S.gameTab.instalockId != -1)
		return S.gameTab.instalockId;
	std::unordered_set<int> unavailable;
	for (const auto& group : champSelect["actions"])
	{
		if (!group.isArray())
			continue;
		for (const auto& action : group)
			if (action["completed"].asBool() && action["championId"].asInt() > 0)
				unavailable.insert(action["championId"].asInt());
	}
	if (unavailable.contains(randomPickId_))
		randomPickId_ = 0;
	if (randomPickId_ > 0)
		return randomPickId_;

	const cpr::Response response = http_.Request("GET", "/lol-champions/v1/owned-champions-minimal");
	if (!IsSuccessful(response))
		return 0;
	const auto champions = League::ChampionService::ParseOwnedChampions(response.text);
	std::vector<int> available;
	for (const auto& champion : champions)
		if (!unavailable.contains(champion.first))
			available.push_back(champion.first);
	if (available.empty())
		return 0;
	randomPickId_ = available[Utils::RandomInt(0, static_cast<int>(available.size() - 1))];
	return randomPickId_;
}

void GameAutomation::ChampSelectAutomation::TryPickIntent(const Json::Value& champSelect)
{
	if (!S.gameTab.instalockEnabled || S.gameTab.instalockId == 0 ||
		champSelect["timer"]["phase"].asString() != "PLANNING" || !champSelect["actions"].isArray())
		return;

	const int localCellId = champSelect["localPlayerCellId"].asInt();
	for (const auto& actionGroup : champSelect["actions"])
	{
		if (!actionGroup.isArray())
			continue;
		for (const auto& action : actionGroup)
		{
			if (action["actorCellId"].asInt() != localCellId ||
				action["type"].asString() != "pick" || action["completed"].asBool())
				continue;

			const int championId = ResolvePickChampionId(champSelect);
			if (championId <= 0 || action["championId"].asInt() == championId)
				return;
			const auto now = std::chrono::steady_clock::now();
			if (now - lastIntentAttempt_ < std::chrono::seconds(1))
				return;
			lastIntentAttempt_ = now;

			Json::Value body;
			body["championId"] = championId;
			// Omitting completed shows pick intent without locking the champion.
			http_.Request("PATCH", std::format("/lol-champ-select/v1/session/actions/{}",
				action["id"].asInt()), ToJson(body));
			return;
		}
	}
}

bool GameAutomation::ChampSelectAutomation::TeamHasPickIntent(const Json::Value& champSelect,
	const int championId)
{
	if (championId <= 0 || !champSelect["myTeam"].isArray())
		return false;

	const int localCellId = champSelect["localPlayerCellId"].asInt();
	for (const auto& player : champSelect["myTeam"])
	{
		if (player["cellId"].asInt() != localCellId && player["championPickIntent"].asInt() == championId)
			return true;
	}
	return false;
}

int GameAutomation::ChampSelectAutomation::GetAutoBanChampionId(const Json::Value& champSelect)
{
	if (S.gameTab.autoBanId <= 0)
		return 0;
	if (!TeamHasPickIntent(champSelect, S.gameTab.autoBanId))
		return S.gameTab.autoBanId;
	if (S.gameTab.backupBanId <= 0 || TeamHasPickIntent(champSelect, S.gameTab.backupBanId))
		return 0;
	return S.gameTab.backupBanId;
}

void GameAutomation::ChampSelectAutomation::ProcessCompletedActions(const Json::Value& champSelect)
{
	if (!champSelect["actions"].isArray())
		return;

	const int localCellId = champSelect["localPlayerCellId"].asInt();
	for (const auto& actionGroup : champSelect["actions"])
	{
		if (!actionGroup.isArray())
			continue;

		for (const auto& action : actionGroup)
		{
			if (!action["completed"].asBool())
				continue;

			const int actionId = action["id"].asInt();
			if (!processedCompletedActions_.insert(actionId).second || action["actorCellId"].asInt() == localCellId ||
				!S.gameTab.instalockEnabled || S.gameTab.instalockId == 0)
			{
				continue;
			}

			const int intendedId = S.gameTab.instalockId == -1 ? randomPickId_ : S.gameTab.instalockId;
			if (intendedId <= 0 || action["championId"].asInt() != intendedId)
				continue;

			if (action["type"].asString() == "ban")
			{
				if (S.gameTab.backupId > 0)
				{
					useBackupId_ = S.gameTab.backupId;
				}
				else if (S.gameTab.instalockId == -1)
				{
					randomPickId_ = 0;
				}
			}
			else if (action["type"].asString() == "pick" && S.gameTab.backupId > 0)
			{
				useBackupId_ = S.gameTab.backupId;
			}
			else if (action["type"].asString() == "pick" && S.gameTab.instalockId == -1)
			{
				randomPickId_ = 0;
			}
		}
	}
}

bool GameAutomation::ChampSelectAutomation::FindAction(const Json::Value& champSelect, const int actionId,
	Json::Value& found)
{
	for (const auto& actionGroup : champSelect["actions"])
	{
		if (!actionGroup.isArray())
			continue;

		for (const auto& action : actionGroup)
		{
			if (action["id"].asInt() == actionId)
			{
				found = action;
				return true;
			}
		}
	}
	return false;
}

void GameAutomation::ChampSelectAutomation::TryChampionAction(const Json::Value& champSelect)
{
	if (!champSelect["actions"].isArray())
		return;

	const int localCellId = champSelect["localPlayerCellId"].asInt();
	for (const auto& actionGroup : champSelect["actions"])
	{
		if (!actionGroup.isArray())
			continue;

		for (const auto& action : actionGroup)
		{
			const int actionId = action["id"].asInt();
			if (action["actorCellId"].asInt() != localCellId || action["completed"].asBool() ||
				!action["isInProgress"].asBool())
			{
				actionFirstSeen_.erase(actionId);
				continue;
			}

			const std::string actionType = action["type"].asString();
			const bool shouldPick = actionType == "pick" && S.gameTab.instalockEnabled && S.gameTab.instalockId != 0;
			const bool shouldBan = actionType == "ban" && S.gameTab.autoBanEnabled && S.gameTab.autoBanId > 0;
			if (!shouldPick && !shouldBan)
				continue;
			if (shouldBan && champSelect["timer"]["phase"].asString() != "BAN_PICK")
				continue;

			// non-blocking delay: the configured ms counts per action id while the rest of the
			// Other client tasks keep running while the delayed action is pending.
			const auto now = std::chrono::steady_clock::now();
			const auto seen = actionFirstSeen_.try_emplace(actionId, now).first;
			const int delay = shouldPick ? S.gameTab.instalockDelay : S.gameTab.autoBanDelay;
			if (now - seen->second < std::chrono::milliseconds(delay))
				continue;

			// freshness check: the action snapshot can be from a stale push, re-read the session
			// and only fire when the action is still ours and still in progress — this is what
			// keeps the ban PATCH from being sent before we are allowed to ban
			Json::Value latestChampSelect;
			Json::Value latestAction;
			if (!FetchSession(latestChampSelect) || !FindAction(latestChampSelect, actionId, latestAction) ||
				latestAction["completed"].asBool() || !latestAction["isInProgress"].asBool())
			{
				actionFirstSeen_.erase(actionId);
				return;
			}
			if (shouldBan && latestChampSelect["timer"]["phase"].asString() != "BAN_PICK")
				return;

			ProcessCompletedActions(latestChampSelect);
			const int championId = shouldPick
				? ResolvePickChampionId(latestChampSelect)
				: GetAutoBanChampionId(latestChampSelect);
			if (championId <= 0)
				return;

			Json::Value body;
			body["completed"] = true;
			body["championId"] = championId;
			const std::string actionEndpoint = std::format(
				"/lol-champ-select/v1/session/actions/{}", actionId);
			const bool patchAccepted = IsSuccessful(http_.Request("PATCH", actionEndpoint, ToJson(body)));

			if (!shouldBan)
			{
				actionFirstSeen_.erase(actionId);
				return;
			}

			// a 2xx on a ban can still be swallowed server-side when the action flips states;
			// verify and let the next tick retry the same action if it did not stick
			if (patchAccepted)
			{
				for (int attempt = 0; attempt < 6; attempt++)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
					Json::Value verified;
					Json::Value verifiedAction;
					if (!FetchSession(verified) || !FindAction(verified, actionId, verifiedAction))
						break;
					if (verifiedAction["completed"].asBool() && verifiedAction["championId"].asInt() == championId)
						break;
				}
			}
			actionFirstSeen_.erase(actionId);
			return;
		}
	}
}

void GameAutomation::ChampSelectAutomation::TryInstantMute(const Json::Value& champSelect)
{
	if (!S.gameTab.instantMute || !champSelect["myTeam"].isArray())
		return;

	const int localCellId = champSelect["localPlayerCellId"].asInt();
	for (const auto& player : champSelect["myTeam"])
	{
		const int cellId = player["cellId"].asInt();
		if (cellId == localCellId || instantMuteCells_.contains(cellId) ||
			(player["puuid"].asString().empty() && player["obfuscatedPuuid"].asString().empty()))
		{
			continue;
		}

		Json::Value body;
		body["summonerId"] = player["summonerId"];
		body["puuid"] = player["puuid"];
		body["obfuscatedSummonerId"] = player["obfuscatedSummonerId"];
		body["obfuscatedPuuid"] = player["obfuscatedPuuid"];
		if (IsSuccessful(http_.Request("POST", "/lol-champ-select/v1/toggle-player-muted", ToJson(body))))
			instantMuteCells_.insert(cellId);
	}
}

bool GameAutomation::ChampSelectAutomation::FetchSession(Json::Value& champSelect)
{
	const cpr::Response response = http_.Request("GET", "/lol-champ-select/v1/session");
	return IsSuccessful(response) && JsonUtils::Parse(response.text, champSelect) && champSelect.isObject();
}
