#pragma once

#include "Definitions.h"
#include "Includes.h"
#include "LCU.h"
#include "Misc.h"
#include "Config.h"
#include "ChampionLookup.h"
#include "JsonUtils.h"
#include "MiscService.h"
#include "RiotId.h"
#include "RiotIdService.h"

class MiscTab
{
public:
	static constexpr size_t RiotIdInputBufferSize = 128;

	struct CachedRiotIdEligibility
	{
		RiotId::Eligibility value;
		std::chrono::steady_clock::time_point checkedAt{};
	};

	static int LimitRiotIdInput(ImGuiInputTextCallbackData* data)
	{
		if (data->EventFlag != ImGuiInputTextFlags_CallbackEdit)
			return 0;

		const int maxChars = *static_cast<int*>(data->UserData);
		const int allowedBytes = RiotId::ClampUtf8ToMaxChars(data->Buf, data->BufTextLen, maxChars);

		// O ImGui trabalha com buffer em bytes, mas o Riot ID tem limite em caracteres.
		// Por isso cortamos no ultimo caractere UTF-8 completo permitido.
		if (allowedBytes < data->BufTextLen)
			data->DeleteChars(allowedBytes, data->BufTextLen - allowedBytes);

		return 0;
	}

	static bool InputTextWithMaxChars(const char* label, char* buffer, const size_t bufferSize, int maxChars)
	{
		return ImGui::InputText(label, buffer, bufferSize, ImGuiInputTextFlags_CallbackEdit, LimitRiotIdInput,
			&maxChars);
	}

	static void RefreshRiotIdEligibility(CachedRiotIdEligibility& eligibility)
	{
		eligibility.value = RiotIdService::GetRiotClientEligibility();
		eligibility.checkedAt = std::chrono::steady_clock::now();
	}

	static void Render()
	{
		static bool onOpen = true;
		if (ImGui::BeginTabItem("Misc"))
		{
			static std::string result;
			static CachedRiotIdEligibility riotIdEligibility;
			static int eligibilityClientPort = 0;

			if (onOpen || eligibilityClientPort != LCU::league.port)
			{
				riotIdEligibility = {};
				eligibilityClientPort = LCU::league.port;
			}

			// Get processes every 5 seconds
			static auto timeBefore = std::chrono::high_resolution_clock::now();
			std::chrono::duration<float, std::milli> timeDuration = std::chrono::high_resolution_clock::now() - timeBefore;
			if (timeDuration.count() > 5000 || onOpen)
			{
				timeBefore = std::chrono::high_resolution_clock::now();
				LCU::GetLeagueProcesses();
			}

			ImGui::Text("Selected process: ");
			ImGui::SameLine();

			std::string comboProcesses;
			if (LCU::IsProcessGood())
			{
				comboProcesses = std::to_string(LCU::leagueProcesses[LCU::indexLeagueProcesses].first)
					+ " : " + LCU::leagueProcesses[LCU::indexLeagueProcesses].second;
			}
			ImGui::SetNextItemWidth(static_cast<float>(S.Window.width / 3));
			if (ImGui::BeginCombo("##comboProcesses", comboProcesses.c_str(), 0))
			{
				for (size_t n = 0; n < LCU::leagueProcesses.size(); n++)
				{
					const bool is_selected = (LCU::indexLeagueProcesses == n);
					if (ImGui::Selectable((std::to_string(LCU::leagueProcesses[n].first) + " : " + LCU::leagueProcesses[n].second).c_str(),
						is_selected))
					{
						LCU::indexLeagueProcesses = n;
						LCU::SetLeagueClientInfo();
						riotIdEligibility = {};
					}

					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			ImGui::Separator();

			ImGui::Columns(2, nullptr, false);

			if (ImGui::Button("Launch another client"))
			{
				if (!std::filesystem::exists(S.leaguePath))
				{
					result = "Invalid path, change it in Settings tab";
				}
				else
					Utils::OpenUrl(std::format("{}LeagueClient.exe", S.leaguePath).c_str(), "--allow-multiple-clients", SW_SHOWNORMAL);
			}

			if (ImGui::Button("Restart UX"))
				result = MiscService::RestartUx();

			ImGui::NextColumn();

			if (ImGui::Button("Close client"))
				result = MiscService::CloseClient();

			ImGui::Columns(1);

			ImGui::Separator();

			ImGui::Columns(2, nullptr, false);

			if (ImGui::Button("Accept all friend requests"))
			{
				if (MessageBoxA(nullptr, "Are you sure?", "Accepting friend requests", MB_OKCANCEL) == IDOK)
					result = MiscService::AcceptFriendRequests();
			}

			ImGui::NextColumn();

			if (ImGui::Button("Delete all friend requests"))
			{
				if (MessageBoxA(nullptr, "Are you sure?", "Deleting friend requests", MB_OKCANCEL) == IDOK)
					result = MiscService::DeleteFriendRequests();
			}

			ImGui::Columns(1);

			static std::vector<MiscService::FriendGroup> items = { {"**Default", 0} };
			static size_t item_current_idx = 0; // Here we store our selection data as an index.
			auto combo_label = items[item_current_idx].name.c_str();

			if (ImGui::Button("Remove all friends"))
			{
				if (MessageBoxA(nullptr, "Are you sure?", "Removing friends", MB_OKCANCEL) == IDOK)
					result = MiscService::RemoveFriendsFromGroup(items[item_current_idx].id);
			}
			ImGui::SameLine();
			ImGui::Text(" From folder: ");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::CalcTextSize(std::string(20, 'W').c_str(), nullptr, true).x);
			if (ImGui::BeginCombo("##comboGroups", combo_label, 0))
			{
				if (const auto groups = MiscService::GetFriendGroups(); !groups.empty())
				{
					items = groups;
					if (item_current_idx >= items.size())
						item_current_idx = 0;
				}

				for (size_t n = 0; n < items.size(); n++)
				{
					const bool is_selected = (item_current_idx == n);
					if (ImGui::Selectable(items[n].name.c_str(), is_selected))
						item_current_idx = n;

					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			ImGui::Separator();

			static int minimapScale = 100;
			ImGui::Text("In-game minimap scale: ");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(static_cast<float>(S.Window.width / 3));
			ImGui::SliderInt("##sliderMinimapScale", &minimapScale, 0, 350, "%d");
			ImGui::SameLine();
			if (ImGui::Button("Submit##submitMinimapScale"))
				result = MiscService::SetMinimapScale(minimapScale);

			ImGui::Separator();

			static std::vector<std::pair<std::string, std::string>> itemsDisenchant = {
				{"Champion shards", "CHAMPION_RENTAL"}, {"Champion pernaments", "CHAMPION"},
				{"Skin shards", "CHAMPION_SKIN_RENTAL"}, {"Skin pernaments", "CHAMPION_SKIN"},
				{"Eternals", "STATSTONE_SHARD"}, {"Ward shards", "WARD_SKIN_RENTAL"}, {"Ward pernaments", "WARD_SKIN",},
				{"Emotes", "EMOTE"}, {"Icons", "SUMMONER_ICON"}, {"Companions", "COMPANION"}
			};
			static size_t itemIndexDisenchant = 0;
			const char* comboDisenchant = itemsDisenchant[itemIndexDisenchant].first.c_str();

			ImGui::Columns(2, nullptr, false);

			if (ImGui::Button("Disenchant all: "))
			{
				if (MessageBoxA(nullptr, "Are you sure?", "Disenchanting loot", MB_OKCANCEL) == IDOK)
				{
					result = MiscService::DisenchantAll(itemsDisenchant[itemIndexDisenchant].second,
						itemsDisenchant[itemIndexDisenchant].first);
				}
			}

			ImGui::SameLine();

			ImGui::SetNextItemWidth(static_cast<float>(S.Window.width / 3.5));
			if (ImGui::BeginCombo("##comboDisenchant", comboDisenchant, 0))
			{
				for (size_t n = 0; n < itemsDisenchant.size(); n++)
				{
					const bool is_selected = (itemIndexDisenchant == n);
					if (ImGui::Selectable(itemsDisenchant[n].first.c_str(), is_selected))
						itemIndexDisenchant = n;

					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			ImGui::NextColumn();

			if (ImGui::Button("Refund last purchase"))
			{
				if (MessageBoxA(nullptr, "Are you sure?", "Refunding last purchase", MB_OKCANCEL) == IDOK)
					result = MiscService::RefundLastPurchase();
			}
			ImGui::SameLine();
			ImGui::HelpMarker("Can refund anything, even loot");

			ImGui::Columns(1);

			ImGui::Text("Champion name to ID");
			static char bufChampionName[50];
			static size_t lastSize = 0;
			static std::string closestChampion;
			static std::string closestId;
			ImGui::SetNextItemWidth(static_cast<float>(S.Window.width / 3));
			ImGui::InputText("##inputChampionName", bufChampionName, IM_ARRAYSIZE(bufChampionName));
			if (strlen(bufChampionName) < 1)
			{
				closestChampion = "";
				closestId = "";
				lastSize = 0;
			}
			else if (lastSize != strlen(bufChampionName))
			{
				lastSize = strlen(bufChampionName);
				const ChampionLookup::Result lookup = ChampionLookup::FindClosest(champSkins, bufChampionName);
				closestChampion = lookup.name;
				closestId = lookup.id;
			}
			ImGui::SameLine();
			ImGui::TextWrapped("%s ID: %s", closestChampion.c_str(), closestId.c_str());

			ImGui::Separator();

			static char bufGameName[RiotIdInputBufferSize];
			static char bufTagLine[RiotIdInputBufferSize];

			if (riotIdEligibility.checkedAt == std::chrono::steady_clock::time_point{}
				|| std::chrono::steady_clock::now() - riotIdEligibility.checkedAt >= std::chrono::seconds(60))
				RefreshRiotIdEligibility(riotIdEligibility);

			if (riotIdEligibility.value.status == RiotId::EligibilityStatus::Eligible)
				ImGui::TextDisabled("Nickname change available now");
			else if (riotIdEligibility.value.status == RiotId::EligibilityStatus::Unknown)
				ImGui::TextDisabled("Could not check nickname change availability");
			else
			{
				const std::string date = RiotId::FormatLocalDateTimeWithUtcOffset(riotIdEligibility.value.eligibleAfter);
				if (!date.empty())
					ImGui::TextWrapped("Next nickname change available on %s", date.c_str());
				else
					ImGui::TextDisabled("Next nickname change date unavailable");
			}

			const bool canEditRiotId = riotIdEligibility.value.status == RiotId::EligibilityStatus::Eligible;
			if (!canEditRiotId)
			{
				bufGameName[0] = '\0';
				bufTagLine[0] = '\0';
			}

			ImGui::BeginDisabled(!canEditRiotId);
			ImGui::SetNextItemWidth(static_cast<float>(S.Window.width / 4));
			InputTextWithMaxChars("##inputGameName", bufGameName, IM_ARRAYSIZE(bufGameName), RiotId::GameNameMaxChars);

			ImGui::SameLine();
			ImGui::Text("#");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(static_cast<float>(S.Window.width / 5));
			InputTextWithMaxChars("##inputTagLine", bufTagLine, IM_ARRAYSIZE(bufTagLine), RiotId::TagLineMaxChars);

			ImGui::SameLine();
			const bool canSubmitRiotId = strlen(bufGameName) > 0 && strlen(bufTagLine) > 0;
			ImGui::BeginDisabled(!canSubmitRiotId);
			if (ImGui::Button("Change##buttonRiotID"))
			{
				RefreshRiotIdEligibility(riotIdEligibility);
				if (riotIdEligibility.value.status != RiotId::EligibilityStatus::Eligible)
				{
					result = riotIdEligibility.value.status == RiotId::EligibilityStatus::Unknown
						? "Could not check nickname change availability" : "Nickname change is not available yet";
				}
				else
				{
					std::string newRiotId = std::string(bufGameName) + "#" + std::string(bufTagLine);
					if (MessageBoxA(nullptr, std::string("Your new Riot ID will be: " + newRiotId).c_str(), "Are you sure?", MB_OKCANCEL) == IDOK)
					{
						result = RiotIdService::SaveAlias(bufGameName, bufTagLine);
						if (result.empty())
							result = "Riot ID change request sent.";
						RefreshRiotIdEligibility(riotIdEligibility);
					}
				}
			}
			ImGui::EndDisabled();
			ImGui::EndDisabled();

			ImGui::TextDisabled("Game name: %d/%d | Tag: %d/%d",
				RiotId::CountUtf8Chars(bufGameName), RiotId::GameNameMaxChars,
				RiotId::CountUtf8Chars(bufTagLine), RiotId::TagLineMaxChars);

			static std::string sResultJson;
			static char* cResultJson;

			if (!result.empty())
			{
				sResultJson = JsonUtils::FormatOrRaw(result);
				result = "";
			}

			if (!sResultJson.empty())
			{
				cResultJson = sResultJson.data();
				ImGui::InputTextMultiline("##miscResult", cResultJson, sResultJson.size() + 1, ImVec2(600, 185));
			}

			if (onOpen)
				onOpen = false;

			ImGui::EndTabItem();
		}
		else
		{
			onOpen = true;
		}
	}
};
