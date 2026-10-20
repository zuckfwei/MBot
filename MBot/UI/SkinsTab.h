#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Definitions.h"
#include "Includes.h"
#include "ChampionService.h"
#include "Utils.h"

class SkinsTab
{
public:
	static void Render()
	{
		if (!ImGui::BeginTabItem("Skins"))
		{
			// EndTabItem must NEVER be called when BeginTabItem returned false
			bOnOpen = true;
			return;
		}

		if (bOnOpen)
		{
			bOnOpen = false;
			RequestLoad();
		}

		ImGui::Text("Skins owned: %d", skinCount.load());
		ImGui::SameLine();

		static char allNamesSeparator[64] = ",";
		if (ImGui::Button("Copy names to clipboard##skinsTab"))
		{
			std::string allNames;
			{
				std::scoped_lock lock(skinsMutex);
				for (const auto& [name, inventoryType, itemId, ownershipType, isVintage, purchaseDate, quantity, uuid] : skins)
				{
					if (name.empty())
						continue;
					allNames += name + allNamesSeparator;
				}
			}
			Utils::CopyToClipboard(allNames);
		}
		ImGui::SameLine();

		const ImVec2 label_size = ImGui::CalcTextSize("W", nullptr, true);
		ImGui::InputTextMultiline("##separatorSkinsTab", allNamesSeparator, IM_ARRAYSIZE(allNamesSeparator),
			ImVec2(0, label_size.y + ImGui::GetStyle().FramePadding.y * 2.0f), ImGuiInputTextFlags_AllowTabInput);

		ImGui::BeginChild("skinsTableChild", ImVec2(0, 0), false);
		{
			static constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable;
			if (ImGui::BeginTable("skinsTable", 3, tableFlags))
			{
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort,
					0.0f, ColumnID_Name);
				ImGui::TableSetupColumn("Item ID", ImGuiTableColumnFlags_WidthFixed, 100.0f, ColumnID_ItemId);
				ImGui::TableSetupColumn("Purchase date", ImGuiTableColumnFlags_WidthFixed, 160.0f,
					ColumnID_PurchaseDate);
				ImGui::TableHeadersRow(); // click a header cell to sort

				std::scoped_lock lock(skinsMutex);
				SortBySpecs();
				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(skins.size()));
				while (clipper.Step())
				{
					for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; index++)
					{
						const Skin& skin = skins[static_cast<size_t>(index)];
						char timeBuff[50];
						strftime(timeBuff, sizeof(timeBuff), "%G-%m-%d %H:%M:%S", &skin.purchaseDate);

						ImGui::PushID(static_cast<int>(skin.itemId));
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						const std::string displayName = skin.name.empty()
							? "(unknown skin)" : skin.name;
						if (ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns))
							Utils::CopyToClipboard(skin.name);
						ImGui::SameLine();
						ImGui::TextUnformatted(displayName.c_str());
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(std::to_string(skin.itemId).c_str());
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(timeBuff);
						ImGui::PopID();
					}
				}
				ImGui::EndTable(); // inside the BeginTable scope, never unconditional
			}
		}
		ImGui::EndChild();

		ImGui::EndTabItem();
	}

private:
	static void RequestLoad()
	{
		if (loading.exchange(true))
			return; // a fetch is already running
		std::thread([]
			{
				std::vector<Skin> fetched = League::ChampionService::GetOwnedSkins();
				std::ranges::sort(fetched, [](const Skin& lhs, const Skin& rhs) {
					return lhs.name < rhs.name; // sensible default until the user clicks a column
					});
				{
					std::scoped_lock lock(skinsMutex);
					skins = std::move(fetched);
					skinCount.store(static_cast<int>(skins.size()));
				}
				loading.store(false);
			}).detach();
	}

	// applies the header click sort; caller must hold skinsMutex
	static void SortBySpecs()
	{
		if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs())
		{
			if (sortSpecs->SpecsDirty && sortSpecs->SpecsCount > 0)
			{
				const int columnId = static_cast<int>(sortSpecs->Specs->ColumnUserID);
				const bool ascending = sortSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending;

				switch (columnId)
				{
				case ColumnID_Name:
					std::ranges::sort(skins, [ascending](const Skin& lhs, const Skin& rhs) {
						return ascending ? lhs.name < rhs.name : rhs.name < lhs.name;
						});
					break;
				case ColumnID_ItemId:
					std::ranges::sort(skins, [ascending](const Skin& lhs, const Skin& rhs) {
						return ascending ? lhs.itemId < rhs.itemId : rhs.itemId < lhs.itemId;
						});
					break;
				case ColumnID_PurchaseDate:
					std::ranges::sort(skins, [ascending](Skin lhs, Skin rhs) {
						const time_t lhsTime = mktime(&lhs.purchaseDate);
						const time_t rhsTime = mktime(&rhs.purchaseDate);
						return ascending ? lhsTime < rhsTime : rhsTime < lhsTime;
						});
					break;
				default:;
				}
				sortSpecs->SpecsDirty = false;
			}
		}
	}

	static constexpr int ColumnID_Name = 0;
	static constexpr int ColumnID_ItemId = 1;
	static constexpr int ColumnID_PurchaseDate = 2;

	static inline bool bOnOpen = true;
	static inline std::vector<Skin> skins;
	static inline std::mutex skinsMutex;
	static inline std::atomic<int> skinCount{ 0 };
	static inline std::atomic<bool> loading{ false };
};
