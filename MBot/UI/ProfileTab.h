#pragma once

#include "Config.h"
#include "Definitions.h"
#include "Includes.h"
#include "League/PlayerService.h"

class ProfileTab
{
public:
	static void Render()
	{
		if (ImGui::BeginTabItem("Profile"))
		{
			static char statusText[1024 * 16];
			ImGui::Text("Status:");
			const ImVec2 label_size = ImGui::CalcTextSize("W", nullptr, true);

			ImGui::InputTextMultiline("##inputStatus", statusText, IM_ARRAYSIZE(statusText), ImVec2(S.Window.width - 230.f,
				(label_size.y + ImGui::GetStyle().FramePadding.y) * 6.f), ImGuiInputTextFlags_AllowTabInput);
			if (ImGui::Button("Submit status"))
			{
				const std::string result = League::PlayerService::SetStatus(statusText);
				if (result.find("errorCode") != std::string::npos)
					MessageBoxA(nullptr, result.c_str(), nullptr, 0);
			}

			ImGui::SameLine();
			static int availability = 0;
			static int lastAvailability = 0;
			ImGui::RadioButton("Online", &availability, 0);
			ImGui::SameLine();
			ImGui::RadioButton("Mobile", &availability, 1);
			ImGui::SameLine();
			ImGui::RadioButton("Away", &availability, 2);
			ImGui::SameLine();
			ImGui::RadioButton("Offline", &availability, 3);

			if (availability != lastAvailability)
			{
				lastAvailability = availability;
				League::PlayerService::SetAvailability(
					static_cast<League::PlayerService::Availability>(availability));
			}

			ImGui::SameLine();
			ImGui::Text("\t   ");
			ImGui::SameLine();
			if (ImGui::Button("Disconnect from chat"))
				League::PlayerService::SuspendChat();

			ImGui::SameLine();
			if (ImGui::Button("Reconnect to chat"))
				League::PlayerService::ResumeChat();

			ImGui::Separator();

			if (ImGui::Button("Invisible banner"))
				League::PlayerService::SetInvisibleBanner();

			ImGui::SameLine();
			ImGui::HelpMarker("Works if last season's rank is unranked");

			ImGui::Separator();

			static int iconID;
			ImGui::Text("Icon:");
			ImGui::InputInt("##inputIcon:", &iconID, 1, 100);
			ImGui::SameLine();
			if (ImGui::Button("Submit##submitIcon"))
			{
				const std::string result = League::PlayerService::SetProfileIcon(iconID);
				if (result.find("errorCode") != std::string::npos)
				{
					MessageBoxA(nullptr, result.c_str(), nullptr, 0);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Submit 2##submitIcon2"))
			{
				const std::string result = League::PlayerService::SetChatIcon(iconID);
				if (result.find("errorCode") != std::string::npos)
				{
					MessageBoxA(nullptr, result.c_str(), nullptr, 0);
				}
			}

			if (ImGui::CollapsingHeader("Backgrounds"))
			{
				if (champSkins.empty())
				{
					ImGui::Text("Skin data is still being fetched");
				}
				else
				{
					for (const auto& [key, name, skins] : champSkins)
					{
						if (ImGui::TreeNode(name.c_str()))
						{
							for (const auto& [fst, snd] : skins)
							{
								if (ImGui::Button(snd.c_str()))
									League::PlayerService::SetBackgroundSkin(fst);
							}
							ImGui::TreePop();
						}
					}
				}
			}

			ImGui::EndTabItem();
		}
	}
};
