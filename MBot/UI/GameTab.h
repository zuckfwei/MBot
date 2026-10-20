#pragma once

#include "Definitions.h"
#include "Includes.h"
#include "Utils.h"
#include "Misc.h"
#include "League/ChampionService.h"
#include "League/GameService.h"

class GameTab
{
private:
	static inline bool onOpen = true;

public:
	static void Render()
	{
		if (ImGui::BeginTabItem("Game"))
		{
			static std::vector<std::string> itemsMultiSearch = {
				"OP.GG", "U.GG", "PORO.GG", "Porofessor.gg"
			};
			if (S.gameTab.indexMultiSearch >= itemsMultiSearch.size())
				S.gameTab.indexMultiSearch = 0;
			const char* selectedMultiSearch = itemsMultiSearch[S.gameTab.indexMultiSearch].c_str();
			const size_t champDataSize = champSkins.size();
			static size_t lastChampDataSize = 0;
			const bool champDataChanged = champDataSize != lastChampDataSize;

			if (ImGui::Button("Multi-Search"))
			{
				const std::string multiSearchResult = GameService::BuildMultiSearchUrl(itemsMultiSearch[S.gameTab.indexMultiSearch]);
				if (multiSearchResult.find("https://") != 0)
				{
					MessageBoxA(nullptr, multiSearchResult.c_str(), "Multi-Search failed", MB_OK);
				}
				else
				{
					Utils::OpenUrl(multiSearchResult.c_str(), nullptr, SW_SHOW);
				}
			}

			ImGui::SameLine();

			ImGui::SetNextItemWidth(static_cast<float>(S.Window.width / 6));
			if (ImGui::BeginCombo("##comboMultiSearch", selectedMultiSearch, 0))
			{
				for (size_t n = 0; n < itemsMultiSearch.size(); n++)
				{
					const bool is_selected = (S.gameTab.indexMultiSearch == n);
					if (ImGui::Selectable(itemsMultiSearch[n].c_str(), is_selected))
						S.gameTab.indexMultiSearch = n;

					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			ImGui::Separator();
			ImGui::TextDisabled("Automation");

			ImGui::Checkbox("Auto accept", &S.gameTab.autoAcceptEnabled);

			ImGui::Checkbox("Insta trade first pick", &S.gameTab.instaTradeFirstPick);
			ImGui::SameLine();
			ImGui::HelpMarker("Requests a pick-order swap with your team's first picker as soon as champion select starts. Only works in 5v5 Draft, Ranked Solo and Ranked Flex.");

			ImGui::Separator();

			const bool isStillBeingFetched = champSkins.empty();

			static ImGui::ComboAutoSelectData instalockComboData;
			static int lastInstalockComboId = -999999;
			static auto lastInstalockComboRefresh = std::chrono::steady_clock::time_point{};
			const auto now = std::chrono::steady_clock::now();

			if ((onOpen || instalockComboData.items.empty() || lastInstalockComboId != S.gameTab.instalockId) &&
				(onOpen || now - lastInstalockComboRefresh > std::chrono::milliseconds(1000)))
			{
				lastInstalockComboRefresh = now;
				std::vector<std::pair<int, std::string>> instalockChamps = League::ChampionService::GetOwnedChampionsCached();

				if (!instalockChamps.empty())
				{
					std::vector<std::string> instalockChampsNames;
					instalockChampsNames.reserve(instalockChamps.size() + 1);
					instalockChampsNames.emplace_back("Random");

					if (S.gameTab.instalockId == -1)
					{
						SetComboInput(instalockComboData, "Random");
						instalockComboData.index = 0;
					}
					else
					{
						std::string selectedChamp = Misc::ChampIdToName(S.gameTab.instalockId);
						SetComboInput(instalockComboData, selectedChamp);
					}

					for (size_t i = 0; i < instalockChamps.size(); i++)
					{
						instalockChampsNames.emplace_back(instalockChamps[i].second);
						if (instalockComboData.input == instalockChamps[i].second)
						{
							instalockComboData.index = static_cast<int>(i + 1);
						}
					}
					instalockComboData.items = instalockChampsNames;
					lastInstalockComboId = S.gameTab.instalockId;
				}
			}

			ImGui::Checkbox("Instalock", &S.gameTab.instalockEnabled);
			ImGui::SameLine();
			ImGui::HelpMarker("Shows your chosen champion as pick intent during planning, before bans. Locks it when your pick turn starts.");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(static_cast<float>(S.Window.width / 6));
			if (ImGui::ComboAutoSelect("##comboInstalock", instalockComboData))
			{
				if (instalockComboData.index != -1)
				{
					if (std::string(instalockComboData.input) == "Random")
					{
						S.gameTab.instalockId = -1;
					}
					else
					{
						for (const auto& [key, name, skins] : champSkins)
						{
							if (instalockComboData.input == name)
							{
								S.gameTab.instalockId = key;
							}
						}
					}
				}
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(static_cast<float>(S.Window.width / 6));

			ImGui::SliderInt("Delay##sliderInstalockDelay", &S.gameTab.instalockDelay, 0, 10000, "%d ms");

			static std::string chosenBackup = "Backup pick \t\t\tChosen: " + Misc::ChampIdToName(S.gameTab.backupId) + "###AnimatedBackup";
			static int lastBackupId = 0;
			if ((lastBackupId != S.gameTab.backupId || champDataChanged) && !isStillBeingFetched)
			{
				lastBackupId = S.gameTab.backupId;
				chosenBackup = "Backup pick \t\t\tChosen: " + Misc::ChampIdToName(S.gameTab.backupId) + "###AnimatedBackup";
			}
			if (ImGui::CollapsingHeader(chosenBackup.c_str()))
			{
				ImGui::Text("None");
				ImGui::SameLine();
				ImGui::RadioButton("##noneBackupPick", &S.gameTab.backupId, 0);
				std::vector<std::pair<int, std::string>> instalockChamps = League::ChampionService::GetOwnedChampionsCached();
				for (const auto& [fst, snd] : instalockChamps)
				{
					char bufchamp[128];
					sprintf_s(bufchamp, "##Select %s", snd.c_str());
					ImGui::Text("%s", snd.c_str());
					ImGui::SameLine();
					ImGui::RadioButton(bufchamp, &S.gameTab.backupId, fst);
				}
			}

			ImGui::Checkbox("Auto ban", &S.gameTab.autoBanEnabled);
			ImGui::SameLine();

			static ImGui::ComboAutoSelectData autobanComboData;
			static int lastAutoBanComboId = -999999;
			if (onOpen || champDataChanged || autobanComboData.items.empty() || lastAutoBanComboId != S.gameTab.autoBanId)
			{
				std::vector<std::string> autobanChampsNames;
				if (!champSkins.empty())
				{
					autobanChampsNames.reserve(champSkins.size() + 1);
					autobanChampsNames.emplace_back("None");
					autobanComboData.index = 0;

					if (S.gameTab.autoBanId <= 0)
					{
						SetComboInput(autobanComboData, "None");
					}
					else
					{
						std::string selectedChamp = Misc::ChampIdToName(S.gameTab.autoBanId);
						SetComboInput(autobanComboData, selectedChamp);
					}

					for (size_t i = 0; i < champSkins.size(); i++)
					{
						autobanChampsNames.emplace_back(champSkins[i].name);

						if (autobanComboData.input == champSkins[i].name)
						{
							autobanComboData.index = static_cast<int>(i + 1);
						}
					}
					autobanComboData.items = autobanChampsNames;
					lastAutoBanComboId = S.gameTab.autoBanId;
				}
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(static_cast<float>(S.Window.width / 6));
			if (ImGui::ComboAutoSelect("##comboAutoban", autobanComboData))
			{
				if (autobanComboData.index != -1)
				{
					if (std::string(autobanComboData.input) == "None")
					{
						S.gameTab.autoBanId = 0;
					}
					else
					{
						for (const auto& [key, name, skins] : champSkins)
						{
							if (autobanComboData.input == name)
							{
								S.gameTab.autoBanId = key;
							}
						}
					}
				}
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(static_cast<float>(S.Window.width / 6));
			ImGui::SliderInt("Delay##sliderautoBanDelay", &S.gameTab.autoBanDelay, 0, 10000, "%d ms");

			static std::string chosenBackupBan = "Backup ban \t\t\tChosen: " + Misc::ChampIdToName(S.gameTab.backupBanId) + "###AnimatedBackupBan";
			static int lastBackupBanId = 0;
			if ((lastBackupBanId != S.gameTab.backupBanId || champDataChanged) && !isStillBeingFetched)
			{
				lastBackupBanId = S.gameTab.backupBanId;
				chosenBackupBan = "Backup ban \t\t\tChosen: " + Misc::ChampIdToName(S.gameTab.backupBanId) + "###AnimatedBackupBan";
			}
			if (ImGui::CollapsingHeader(chosenBackupBan.c_str()))
			{
				ImGui::Text("None");
				ImGui::SameLine();
				ImGui::RadioButton("##noneBackupBan", &S.gameTab.backupBanId, 0);
				for (const auto& [key, name, skins] : champSkins)
				{
					char bufchamp[128];
					sprintf_s(bufchamp, "##SelectBackupBan%d", key);
					ImGui::Text("%s", name.c_str());
					ImGui::SameLine();
					ImGui::RadioButton(bufchamp, &S.gameTab.backupBanId, key);
				}
			}

			ImGui::Checkbox("Instant Mute", &S.gameTab.instantMute);

			lastChampDataSize = champDataSize;

			if (onOpen)
				onOpen = false;

			ImGui::EndTabItem();
		}
		else
		{
			onOpen = true;
		}
	}

	static void SetComboInput(ImGui::ComboAutoSelectData& data, const std::string& value)
	{
		strncpy_s(data.input, sizeof(data.input), value.c_str(), _TRUNCATE);
	}
};
