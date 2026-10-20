#pragma once

#include <string>

#include <json/json.h>

namespace League
{
	class LcuHttpClient;
}

namespace GameAutomation
{
	class ReadyCheckAutomation
	{
	public:
		explicit ReadyCheckAutomation(League::LcuHttpClient& http);

		void HandlePhase(const std::string& phase);
		void HandleReadyCheck(const Json::Value& readyCheck);

	private:
		void TryAccept();

		League::LcuHttpClient& http_;
		bool accepted_ = false;
	};
}
