#pragma once

#include <chrono>

#include <json/json.h>

namespace League
{
	class LcuHttpClient;
}

namespace GameAutomation
{
	class PickOrderSwapAutomation
	{
	public:
		explicit PickOrderSwapAutomation(League::LcuHttpClient& http);

		void Reset();
		void Handle(const Json::Value& champSelect);

	private:
		void EnsureQueueEligibility();
		static int GetFirstTeamPickCellId(const Json::Value& champSelect);

		League::LcuHttpClient& http_;
		int queueEligibility_ = -1;
		bool requested_ = false;
		std::chrono::steady_clock::time_point lastAttempt_{};
	};
}
