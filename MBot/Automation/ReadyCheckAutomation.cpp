#include "ReadyCheckAutomation.h"

#include <Windows.h>

#include "Config.h"
#include "LcuHttpClient.h"

namespace
{
	bool IsSuccessful(const cpr::Response& response)
	{
		return response.status_code >= 200 && response.status_code < 300;
	}
}

GameAutomation::ReadyCheckAutomation::ReadyCheckAutomation(League::LcuHttpClient& http) : http_(http)
{
}

void GameAutomation::ReadyCheckAutomation::HandlePhase(const std::string& phase)
{
	if (phase == "ReadyCheck")
		TryAccept();
	else
		accepted_ = false;
}

void GameAutomation::ReadyCheckAutomation::HandleReadyCheck(const Json::Value& readyCheck)
{
	if (readyCheck["state"].asString() == "InProgress")
		TryAccept();
}

void GameAutomation::ReadyCheckAutomation::TryAccept()
{
	if (!S.gameTab.autoAcceptEnabled || accepted_)
		return;

	if (IsSuccessful(http_.Request("POST", "/lol-matchmaking/v1/ready-check/accept")))
		accepted_ = true;
}
