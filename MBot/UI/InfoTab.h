#pragma once

#include "Includes.h"
#include "Utils.h"
#include "Config.h"
#include "League/PlayerService.h"

class InfoTab
{
public:
	static void Render()
	{
		if (ImGui::BeginTabItem("Info"))
		{
			static bool once = true;
			static League::PlayerService::PlayerInfo player;
			static std::string sResultJson;

			static char playerName[50];
			if (once)
			{
				once = false;
				std::ranges::copy(S.infoTab.playerName, playerName);
			}

			ImGui::Text("Input player name:");
			ImGui::InputText("##inputPlayerName", playerName, IM_ARRAYSIZE(playerName));
			S.infoTab.playerName = playerName;
			ImGui::SameLine();

			if (ImGui::Button("Submit##playerName") || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter), false))
			{
				player = League::PlayerService::FindByName(playerName);
				sResultJson = player.formatted;
			}

			ImGui::SameLine();
			if (ImGui::Button("puuid##playerName"))
			{
				player = League::PlayerService::FindByPuuid(playerName);
				sResultJson = player.formatted;
			}
			ImGui::SameLine();
			if (ImGui::Button("summId##playerName"))
			{
				player = League::PlayerService::FindBySummonerId(playerName);
				sResultJson = player.formatted;
			}
			ImGui::SameLine();
			if (ImGui::Button("Me##playerName"))
			{
				player = League::PlayerService::GetCurrentPlayer();
				sResultJson = player.formatted;
			}

			if (!sResultJson.empty())
			{
				ImGui::InputTextMultiline("##infoResult", sResultJson.data(), sResultJson.size() + 1, ImVec2(600, 350));
			}

			if (ImGui::Button("Invite to lobby##infoTab"))
				League::PlayerService::InviteToLobby(player);
			ImGui::SameLine();
			if (ImGui::Button("Invite to friends##infoTab"))
				League::PlayerService::SendFriendRequest(player);
			ImGui::SameLine();
			if (ImGui::Button("Add to block list##infoTab"))
				League::PlayerService::BlockPlayer(player);

			if (ImGui::Button("Copy to clipboard##infoTab"))
			{
				Utils::CopyToClipboard(sResultJson);
			}

			ImGui::EndTabItem();
		}
	}
};
