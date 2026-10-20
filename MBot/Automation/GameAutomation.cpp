#include "GameAutomation.h"

#include "ChampSelectAutomation.h"
#include "ChampionService.h"
#include "LcuWebSocket.h"
#include "PickOrderSwapAutomation.h"
#include "ReadyCheckAutomation.h"
#include "JsonUtils.h"
#include "LcuHttpClient.h"
#include "LCU.h"

#include <chrono>
#include <mutex>
#include <optional>
#include <thread>

namespace
{
	bool IsSuccessful(const cpr::Response& response)
	{
		return response.status_code >= 200 && response.status_code < 300;
	}

	class Controller
	{
	public:
		Controller() : readyCheck_(http_), champSelect_(http_), pickOrderSwap_(http_)
		{
			// WebSocket pushes fill the mailbox; the controller thread drains it on its own cadence.
			// Automation stays single-threaded, so game state is only touched from Run().
			webSocket_.SetHandlers(
				[this](const std::string& phase)
				{
					const std::scoped_lock lock(stateMutex_);
					pendingPhase_ = phase;
					hasPhase_ = true;
				},
				[this](Json::Value session)
				{
					const std::scoped_lock lock(stateMutex_);
					pendingSession_ = std::move(session);
					hasSession_ = true;
				},
				[this](Json::Value readyCheck)
				{
					const std::scoped_lock lock(stateMutex_);
					pendingReadyCheck_ = std::move(readyCheck);
					hasReadyCheck_ = true;
				});
		}

		void Run()
		{
			while (true)
			{
				const ClientInfo client = LCU::league;
				if (client.port <= 0 || client.token.empty())
				{
					webSocket_.UpdateCredentials(0, {});
					Reset();
					std::this_thread::sleep_for(std::chrono::seconds(1));
					continue;
				}

				if (client.port != port_ || client.token != token_)
				{
					Reset();
					http_.Configure(client);
					port_ = client.port;
					token_ = client.token;
					webSocket_.UpdateCredentials(port_, token_);
					League::ChampionService::PrefetchOwnedChampions();
				}

				DrainNotifications();

				bool sessionRequested = false;
				{
					const std::scoped_lock lock(stateMutex_);
					sessionRequested = sessionRequestPending_;
					sessionRequestPending_ = false;
				}
				if (sessionRequested)
					FetchSessionAndProcess();

				// WebSocket pulses drive the loop; polling only runs while the socket is down.
				if (!webSocket_.IsLive())
					PollFallback();
				else
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

	private:
		void DrainNotifications()
		{
			std::optional<std::string> phase;
			Json::Value readyCheck;
			Json::Value session;
			bool processReadyCheck = false;
			bool processSession = false;
			{
				const std::scoped_lock lock(stateMutex_);
				if (hasPhase_)
				{
					phase = std::move(pendingPhase_);
					hasPhase_ = false;
				}
				if (hasReadyCheck_)
				{
					readyCheck = std::move(pendingReadyCheck_);
					hasReadyCheck_ = false;
					processReadyCheck = true;
				}
				if (hasSession_)
				{
					session = std::move(pendingSession_);
					hasSession_ = false;
					processSession = true;
				}
			}

			// order matters: phase first so champ-select resets happen before session handling
			if (phase)
				ProcessPhase(*phase);
			if (processReadyCheck)
				readyCheck_.HandleReadyCheck(readyCheck);
			if (processSession)
				ProcessSession(session);
		}

		void ProcessPhase(const std::string& phase)
		{
			if (phase == "ChampSelect")
			{
				if (lastPhase_ != "ChampSelect")
					sessionRequestPending_ = true; // fresh champ select: pull the session immediately
			}
			else if (gameId_ != 0)
			{
				ResetChampSelect();
			}
			lastPhase_ = phase;
			readyCheck_.HandlePhase(phase);
		}

		void ProcessSession(const Json::Value& session)
		{
			if (!session.isObject())
			{
				// null data means the session is gone
				if (gameId_ != 0)
					ResetChampSelect();
				return;
			}

			const Json::Int64 gameId = session["gameId"].asInt64();
			if (gameId != gameId_)
			{
				ResetChampSelect();
				gameId_ = gameId;
			}
			pickOrderSwap_.Handle(session);
			champSelect_.Handle(session);
		}

		void FetchSessionAndProcess()
		{
			const cpr::Response response = http_.Request("GET", "/lol-champ-select/v1/session");
			Json::Value session;
			if (IsSuccessful(response) && JsonUtils::Parse(response.text, session))
				ProcessSession(session);
		}

		// same polling route as before, used whenever the WebSocket is not connected
		void PollFallback()
		{
			Json::Value phase;
			const cpr::Response response = http_.Request("GET", "/lol-gameflow/v1/gameflow-phase");
			if (!IsSuccessful(response) || !JsonUtils::Parse(response.text, phase) || !phase.isString())
			{
				std::this_thread::sleep_for(std::chrono::seconds(1));
				return;
			}

			const std::string currentPhase = phase.asString();
			ProcessPhase(currentPhase);
			if (currentPhase == "ChampSelect")
			{
				Json::Value session;
				const cpr::Response sessionResponse = http_.Request("GET", "/lol-champ-select/v1/session");
				if (IsSuccessful(sessionResponse) && JsonUtils::Parse(sessionResponse.text, session))
					ProcessSession(session);
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}
			else
			{
				std::this_thread::sleep_for(currentPhase == "ReadyCheck"
					? std::chrono::milliseconds(250) : std::chrono::milliseconds(1000));
			}
		}

		void ResetChampSelect()
		{
			gameId_ = 0;
			pickOrderSwap_.Reset();
			champSelect_.Reset();
		}

		void Reset()
		{
			port_ = 0;
			token_.clear();
			lastPhase_.clear();
			sessionRequestPending_ = false;
			readyCheck_.HandlePhase("None");
			ResetChampSelect();
		}

		League::LcuHttpClient http_;
		League::LcuWebSocket webSocket_;
		GameAutomation::ReadyCheckAutomation readyCheck_;
		GameAutomation::ChampSelectAutomation champSelect_;
		GameAutomation::PickOrderSwapAutomation pickOrderSwap_;

		std::mutex stateMutex_;
		std::string pendingPhase_;
		Json::Value pendingReadyCheck_;
		Json::Value pendingSession_;
		bool hasPhase_ = false;
		bool hasReadyCheck_ = false;
		bool hasSession_ = false;
		bool sessionRequestPending_ = false;

		int port_ = 0;
		std::string token_;
		std::string lastPhase_;
		Json::Int64 gameId_ = 0;
	};
}

void GameAutomation::Run()
{
	Controller controller;
	controller.Run();
}
