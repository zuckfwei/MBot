#pragma once

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

#include "Config.h"
#include "CustomRequestService.h"
#include "Includes.h"
#include "JsonUtils.h"
#include "Utils.h"

class CustomTab
{
public:
	static void Custom()
	{
		static bool initialized = false;
		static char method[50] = {};
		static char urlText[1024 * 16] = {};
		static char requestText[1024 * 32] = {};
		static char inputPort[64] = {};
		static char inputHeader[1024 * 16] = {};

		static bool useRiotClient = false;
		static bool customConnectionOpen = false;
		static std::string selectedPresetUrl;
		static std::string result;

		if (!initialized)
		{
			initialized = true;
			CopyToBuffer(method, S.customTab.method);
			CopyToBuffer(urlText, S.customTab.urlText);
			CopyToBuffer(requestText, S.customTab.requestText);
			CopyToBuffer(inputPort, S.customTab.port);
			CopyToBuffer(inputHeader, S.customTab.header);
			selectedPresetUrl = urlText[0] == '\0' ? std::string(LocalhostUrl) : std::string(urlText);
		}

		const auto applyPreset = [&](const CustomRequestService::Preset preset) {
			CustomRequestService::Connection connection;
			std::string error;
			if (!CustomRequestService::GetConnection(preset, connection, error))
			{
				result = error;
				return false;
			}

			const std::string currentUrl = urlText;
			if (currentUrl.empty() || currentUrl == selectedPresetUrl || currentUrl == LocalhostUrl)
				CopyToBuffer(urlText, connection.url);

			selectedPresetUrl = connection.url;
			CopyToBuffer(inputPort, connection.port);
			CopyToBuffer(inputHeader, connection.header);
			return true;
		};

		ImGui::Text("Client:");
		ImGui::SameLine();
		if (ImGui::RadioButton("LCU##customClient", !useRiotClient))
		{
			useRiotClient = false;
			if (customConnectionOpen)
				applyPreset(CustomRequestService::Preset::League);
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Riot##customClient", useRiotClient))
		{
			useRiotClient = true;
			if (customConnectionOpen && !applyPreset(CustomRequestService::Preset::Riot))
				useRiotClient = false;
		}

		ImGui::Text("Method:");
		const ImVec2 labelSize = ImGui::CalcTextSize("W", nullptr, true);
		ImGui::InputTextEx("##inputMethod", nullptr, method, IM_ARRAYSIZE(method),
			ImVec2(S.Window.width - 130.f, labelSize.y + ImGui::GetStyle().FramePadding.y * 2.0f),
			0, nullptr, nullptr);

		ImGui::Text("URL:");
		ImGui::InputTextMultiline("##inputUrl", urlText, IM_ARRAYSIZE(urlText),
			ImVec2(S.Window.width - 130.f, labelSize.y + ImGui::GetStyle().FramePadding.y * 2.0f));

		ImGui::Text("Body:");
		ImGui::InputTextMultiline("##inputBody", requestText, IM_ARRAYSIZE(requestText),
			ImVec2(S.Window.width - 130.f, (labelSize.y + ImGui::GetStyle().FramePadding.y) * 6.f),
			ImGuiInputTextFlags_AllowTabInput);

		customConnectionOpen = ImGui::CollapsingHeader("Custom Port/Header");
		if (customConnectionOpen)
		{
			ImGui::Text("Port:");
			ImGui::InputText("##inputPort", inputPort, IM_ARRAYSIZE(inputPort), ImGuiInputTextFlags_CharsDecimal);
			ImGui::SameLine();

			if (ImGui::Button("LCU"))
			{
				useRiotClient = false;
				applyPreset(CustomRequestService::Preset::League);
			}
			ImGui::SameLine();
			if (ImGui::Button("Riot"))
			{
				if (applyPreset(CustomRequestService::Preset::Riot))
					useRiotClient = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Store"))
				applyPreset(CustomRequestService::Preset::Store);
			ImGui::SameLine();
			if (ImGui::Button("Ledge"))
				applyPreset(CustomRequestService::Preset::Ledge);

			ImGui::Text("Header:");
			ImGui::InputTextMultiline("##inputHeader", inputHeader, IM_ARRAYSIZE(inputHeader),
				ImVec2(S.Window.width - 130.f, (labelSize.y + ImGui::GetStyle().FramePadding.y) * 6.f),
				ImGuiInputTextFlags_AllowTabInput);
		}

		S.customTab.method = method;
		S.customTab.urlText = urlText;
		S.customTab.requestText = requestText;
		S.customTab.port = inputPort;
		S.customTab.header = inputHeader;

		ImGui::Columns(2, nullptr, false);
		if (ImGui::Button("Send custom request##customTab"))
		{
			std::string port = inputPort;
			std::string header = inputHeader;
			std::string error;

			if (!customConnectionOpen)
			{
				CustomRequestService::Connection connection;
				const auto preset = useRiotClient
					? CustomRequestService::Preset::Riot
					: CustomRequestService::Preset::League;
				if (!CustomRequestService::GetConnection(preset, connection, error))
					result = error;
				else
				{
					port = connection.port;
					header = connection.header;
					result = CustomRequestService::Send(method, urlText, requestText, port, header, error);
				}
			}
			else
			{
				result = CustomRequestService::Send(method, urlText, requestText, port, header, error);
			}

			if (!error.empty())
				result = error;
			else if (!result.empty() && !customConnectionOpen)
				result = FormatJson(result);
		}

		ImGui::SameLine();
		if (ImGui::Button("Format JSON##customTab"))
			result = FormatJson(result);

		ImGui::NextColumn();
		ImGui::Text("Endpoints list:");
		ImGui::TextURL("LCU", "https://lcu.kebs.dev", 1, 0);
		ImGui::SameLine();
		ImGui::Text(" | ");
		ImGui::TextURL("Riot Client", "https://riotclient.kebs.dev", 1, 0);
		ImGui::Columns(1);

		if (!result.empty())
		{
			ImGui::InputTextMultiline("##customResult", result.data(), result.size() + 1,
				ImVec2(S.Window.width - 130.f,
					(labelSize.y + ImGui::GetStyle().FramePadding.y) * 19.f));
		}
	}

	static void Invoke()
	{
		static bool initialized = false;
		static char destination[1024] = {};
		static char method[1024] = {};
		static char args[1024 * 32] = {};
		static std::string result;

		if (!initialized)
		{
			initialized = true;
			CopyToBuffer(destination, S.invokeTab.destination);
			CopyToBuffer(method, S.invokeTab.method);
			CopyToBuffer(args, S.invokeTab.args);
		}

		ImGui::Text("Destination:");
		ImGui::InputTextMultiline("##inputDestination", destination, IM_ARRAYSIZE(destination), ImVec2(600, 20));

		ImGui::Text("Method:");
		ImGui::InputTextMultiline("##inputMethod", method, IM_ARRAYSIZE(method), ImVec2(600, 20));

		ImGui::Text("Args:");
		ImGui::InputTextMultiline("##inputArgs", args, IM_ARRAYSIZE(args), ImVec2(600, 50));

		S.invokeTab.destination = destination;
		S.invokeTab.method = method;
		S.invokeTab.args = args;

		if (ImGui::Button("Submit##submitInvoke"))
			result = FormatJson(CustomRequestService::Invoke(destination, method, args));

		ImGui::Text("Result:");
		ImGui::SameLine();
		if (ImGui::Button("Copy to clipboard##invokeTab"))
			Utils::CopyToClipboard(result);

		if (!result.empty())
			ImGui::InputTextMultiline("##gameResult", result.data(), result.size() + 1, ImVec2(600, 232));
	}

	static void Render()
	{
		if (ImGui::BeginTabItem("Custom"))
		{
			if (ImGui::BeginTabBar("TabBar"))
			{
				if (ImGui::BeginTabItem("HTTP/HTTPS"))
				{
					Custom();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("LCDS"))
				{
					Invoke();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
			ImGui::EndTabItem();
		}
	}

private:
	static constexpr std::string_view LocalhostUrl = "https://127.0.0.1";

	template <size_t Size>
	static void CopyToBuffer(char (&buffer)[Size], const std::string_view value)
	{
		const size_t copySize = (std::min)(value.size(), Size - 1);
		std::memcpy(buffer, value.data(), copySize);
		buffer[copySize] = '\0';
	}

	static std::string FormatJson(const std::string& value)
	{
		return JsonUtils::FormatOrRaw(value);
	}
};
