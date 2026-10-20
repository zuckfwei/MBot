#pragma once

#include <unordered_map>
#include <unordered_set>
#include <chrono>

#include <json/json.h>

namespace League
{
	class LcuHttpClient;
}

namespace GameAutomation
{
	class ChampSelectAutomation
	{
	public:
		explicit ChampSelectAutomation(League::LcuHttpClient& http);

		void Reset();
		void Handle(const Json::Value& champSelect);

	private:
		void ProcessCompletedActions(const Json::Value& champSelect);
		void TryPickIntent(const Json::Value& champSelect);
		int ResolvePickChampionId(const Json::Value& champSelect);
		void TryChampionAction(const Json::Value& champSelect);
		void TryInstantMute(const Json::Value& champSelect);
		bool FetchSession(Json::Value& champSelect);

		static bool TeamHasPickIntent(const Json::Value& champSelect, int championId);
		static int GetAutoBanChampionId(const Json::Value& champSelect);
		static bool FindAction(const Json::Value& champSelect, int actionId, Json::Value& found);

		League::LcuHttpClient& http_;
		int useBackupId_ = 0;
		int randomPickId_ = 0;
		std::chrono::steady_clock::time_point lastIntentAttempt_{};
		std::unordered_set<int> instantMuteCells_;
		std::unordered_set<int> processedCompletedActions_;
		std::unordered_map<int, std::chrono::steady_clock::time_point> actionFirstSeen_;
	};
}
