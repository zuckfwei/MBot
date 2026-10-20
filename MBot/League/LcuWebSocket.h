#pragma once

// language: C++20, file: LcuWebSocket.h, target: Windows 11, MSVC
// LCU WebSocket listener over WinHttp: subscribes to OnJsonApiEvent once, filters the pushes
// that drive the bot (gameflow phase / champ-select session / ready check) and hands them to
// callbacks so the automation reacts in milliseconds instead of polling every 200 ms.
// Reconnects on its own whenever the client restarts or the socket drops.

#include <Windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <json/json.h>

#include "JsonUtils.h"

namespace League
{
	class LcuWebSocket
	{
	public:
		using PhaseHandler = std::function<void(const std::string& phase)>;
		using JsonHandler = std::function<void(Json::Value data)>;

		LcuWebSocket() = default;
		~LcuWebSocket()
		{
			Stop();
		}

		LcuWebSocket(const LcuWebSocket&) = delete;
		LcuWebSocket& operator=(const LcuWebSocket&) = delete;

		void SetHandlers(PhaseHandler onPhase, JsonHandler onChampSelect, JsonHandler onReadyCheck)
		{
			const std::scoped_lock lock(handlersMutex_);
			onPhase_ = std::move(onPhase);
			onChampSelect_ = std::move(onChampSelect);
			onReadyCheck_ = std::move(onReadyCheck);
		}

		// called whenever the League client credentials change; restarts the socket on demand
		void UpdateCredentials(const int port, const std::string& basicToken)
		{
			{
				const std::scoped_lock lock(credentialsMutex_);
				if (port == port_ && basicToken == token_)
					return;
				port_ = port;
				token_ = basicToken;
			}
			StopWorker();
			if (port > 0 && !basicToken.empty())
				StartWorker();
		}

		void Stop()
		{
			StopWorker();
			const std::scoped_lock lock(credentialsMutex_);
			port_ = 0;
			token_.clear();
		}

		[[nodiscard]] bool IsLive() const
		{
			return live_.load(std::memory_order_acquire);
		}

	private:
		static std::wstring Widen(const std::string& text)
		{
			if (text.empty())
				return {};
			const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
			std::wstring wide(static_cast<size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), size);
			return wide;
		}

		static void CloseIfOpen(HINTERNET& handle)
		{
			if (handle)
			{
				WinHttpCloseHandle(handle);
				handle = nullptr;
			}
		}

		void StartWorker()
		{
			running_.store(true, std::memory_order_release);
			worker_ = std::thread([this] { Run(); });
		}

		void StopWorker()
		{
			running_.store(false, std::memory_order_release);
			// closing the socket handle from here unblocks a pending WinHttpWebSocketReceive
			if (HINTERNET socketHandle = socketHandle_.exchange(nullptr))
				WinHttpCloseHandle(socketHandle);
			if (HINTERNET connectHandle = connectHandle_.exchange(nullptr))
				WinHttpCloseHandle(connectHandle);
			if (HINTERNET sessionHandle = sessionHandle_.exchange(nullptr))
				WinHttpCloseHandle(sessionHandle);
			if (worker_.joinable())
				worker_.join();
		}

		void Run()
		{
			std::vector<char> chunk(64 * 1024);
			std::string message;
			while (running_.load(std::memory_order_acquire))
			{
				int port = 0;
				std::string token;
				{
					const std::scoped_lock lock(credentialsMutex_);
					port = port_;
					token = token_;
				}
				if (port <= 0 || token.empty())
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}

				live_.store(false, std::memory_order_release);

				HINTERNET sessionHandle = WinHttpOpen(L"MBot/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
					WINHTTP_NO_PROXY_NAME, nullptr, 0);
				HINTERNET connectHandle = sessionHandle
					? WinHttpConnect(sessionHandle, L"127.0.0.1", static_cast<INTERNET_PORT>(port), 0)
					: nullptr;
				HINTERNET requestHandle = connectHandle
					? WinHttpOpenRequest(connectHandle, L"GET", L"/", nullptr, WINHTTP_NO_REFERER,
						WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
					: nullptr;

				if (requestHandle)
				{
					// the LCU presents a self-signed certificate; accept it and request the upgrade
					DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
						SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
					WinHttpSetOption(requestHandle, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));
					DWORD upgrade = TRUE;
					WinHttpSetOption(requestHandle, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, &upgrade, sizeof(upgrade));

					const std::wstring headers = L"Authorization: Basic " + Widen(token) + L"\r\n";
					WinHttpAddRequestHeaders(requestHandle, headers.c_str(), static_cast<DWORD>(-1),
						WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

					if (WinHttpSendRequest(requestHandle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
						WinHttpReceiveResponse(requestHandle, nullptr))
					{
						socketHandle_.store(WinHttpWebSocketCompleteUpgrade(requestHandle, 0),
							std::memory_order_release);
					}
				}
				CloseIfOpen(requestHandle);

				HINTERNET socketHandle = socketHandle_.load(std::memory_order_acquire);
				if (!socketHandle)
				{
					CloseIfOpen(connectHandle);
					CloseIfOpen(sessionHandle);
					std::this_thread::sleep_for(std::chrono::milliseconds(2000));
					continue;
				}

				constexpr char subscribe[] = R"([5,"OnJsonApiEvent"])";
				if (WinHttpWebSocketSend(socketHandle, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
					reinterpret_cast<PBYTE>(const_cast<char*>(subscribe)), sizeof(subscribe) - 1) != NO_ERROR)
				{
					WinHttpCloseHandle(socketHandle);
					socketHandle_.store(nullptr, std::memory_order_release);
					CloseIfOpen(connectHandle);
					CloseIfOpen(sessionHandle);
					std::this_thread::sleep_for(std::chrono::milliseconds(2000));
					continue;
				}

				live_.store(true, std::memory_order_release);

				// frames arrive as JSON arrays: [opcode, uri, data]; opcode 5 is a push
				message.clear();
				while (running_.load(std::memory_order_acquire))
				{
					DWORD bytesRead = 0;
					WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
					if (WinHttpWebSocketReceive(socketHandle, chunk.data(), static_cast<DWORD>(chunk.size()),
						&bytesRead, &bufferType) != NO_ERROR)
					{
						break;
					}

					if (bufferType == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ||
						bufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
					{
						message.append(chunk.data(), bytesRead);
						if (bufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
						{
							HandleMessage(message, ChosenRoute(message));
							message.clear();
						}
						continue;
					}
					break; // close / aborted close
				}

				live_.store(false, std::memory_order_release);
				WinHttpWebSocketClose(socketHandle, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
				socketHandle_.store(nullptr, std::memory_order_release);
				CloseIfOpen(connectHandle);
				CloseIfOpen(sessionHandle);
				std::this_thread::sleep_for(std::chrono::milliseconds(2000));
			}
		}

		enum class Route
		{
			None,
			Phase,
			ChampSelect,
			ReadyCheck
		};

		// cheap substring triage before spending the JSON parse on it
		static Route ChosenRoute(const std::string& text)
		{
			if (text.find("lol-gameflow/v1/gameflow-phase") != std::string::npos)
				return Route::Phase;
			if (text.find("lol-champ-select/v1/session") != std::string::npos)
				return Route::ChampSelect;
			if (text.find("lol-matchmaking/v1/ready-check") != std::string::npos)
				return Route::ReadyCheck;
			return Route::None;
		}

		void HandleMessage(const std::string& payload, const Route route)
		{
			if (route == Route::None || payload.size() > 1024 * 1024)
				return;

			Json::Value root;
			if (!JsonUtils::Parse(payload, root) || !root.isArray() || root.size() < 3 || root[0].asInt() != 5)
				return;

			Json::Value data = root[2];
			switch (route)
			{
			case Route::Phase:
			{
				PhaseHandler handler;
				{
					const std::scoped_lock lock(handlersMutex_);
					handler = onPhase_;
				}
				if (handler && data.isString())
					handler(data.asString());
				break;
			}
			case Route::ChampSelect:
			{
				JsonHandler handler;
				{
					const std::scoped_lock lock(handlersMutex_);
					handler = onChampSelect_;
				}
				if (handler)
					handler(std::move(data));
				break;
			}
			case Route::ReadyCheck:
			{
				JsonHandler handler;
				{
					const std::scoped_lock lock(handlersMutex_);
					handler = onReadyCheck_;
				}
				if (handler)
					handler(std::move(data));
				break;
			}
			default:;
			}
		}

		std::thread worker_;
		std::atomic<bool> running_{ false };
		std::atomic<bool> live_{ false };

		std::atomic<HINTERNET> sessionHandle_{ nullptr };
		std::atomic<HINTERNET> connectHandle_{ nullptr };
		std::atomic<HINTERNET> socketHandle_{ nullptr };

		std::mutex credentialsMutex_;
		int port_ = 0;
		std::string token_;

		std::mutex handlersMutex_;
		PhaseHandler onPhase_;
		JsonHandler onChampSelect_;
		JsonHandler onReadyCheck_;
	};
}
