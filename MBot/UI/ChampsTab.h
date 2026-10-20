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

class ChampsTab
{
public:
	static void Render()
	{
		if (!ImGui::BeginTabItem("Champs"))
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

		ImGui::Text("Champions owned: %u", champsCount.load());
		ImGui::SameLine();

		static char allNamesSeparator[64] = ",";
		if (ImGui::Button("Copy names to clipboard##champsTab"))
		{
			std::string allNames;
			{
				std::scoped_lock lock(championsMutex);
				for (const auto& [min, mas] : champions)
				{
					if (!min.owned)
						continue;
					allNames += min.name + allNamesSeparator;
				}
			}
			Utils::CopyToClipboard(allNames);
		}

		ImGui::BeginChild("champsTableChild", ImVec2(0, 0), false);
		{
			static constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable;
			if (ImGui::BeginTable("champsTable", 3, tableFlags))
			{
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort,
					0.0f, ColumnID_Name);
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 70.0f, ColumnID_ChampionId);
				ImGui::TableSetupColumn("Purchase date", ImGuiTableColumnFlags_WidthFixed, 160.0f,
					ColumnID_PurchaseDate);
				ImGui::TableHeadersRow(); // click a header cell to sort

				std::scoped_lock lock(championsMutex);
				SortBySpecs();
				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(champions.size()));
				while (clipper.Step())
				{
					for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; index++)
					{
						const ChampAll& entry = champions[static_cast<size_t>(index)];
						const ChampMinimal& min = entry.min;

						ImGui::PushID(static_cast<int>(min.id));
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						if (ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns))
							Utils::CopyToClipboard(min.name);
						ImGui::SameLine();
						ImGui::TextUnformatted(min.name.c_str());
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(std::to_string(min.id).c_str());
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(FormatPurchaseDate(min.purchased).c_str());
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
				std::vector<ChampAll> fetched = League::ChampionService::GetChampionCollection();
				{
					std::scoped_lock lock(championsMutex);
					champions = std::move(fetched);
					champsCount.store(static_cast<unsigned>(champions.size()));
				}
				loading.store(false);
			}).detach();
	}

	// applies the header click sort; caller must hold championsMutex
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
					std::ranges::sort(champions, [ascending](const ChampAll& lhs, const ChampAll& rhs) {
						return ascending ? lhs.min.name < rhs.min.name : rhs.min.name < lhs.min.name;
						});
					break;
				case ColumnID_ChampionId:
					std::ranges::sort(champions, [ascending](const ChampAll& lhs, const ChampAll& rhs) {
						return ascending ? lhs.min.id < rhs.min.id : rhs.min.id < lhs.min.id;
						});
					break;
				case ColumnID_PurchaseDate:
					std::ranges::sort(champions, [ascending](const ChampAll& lhs, const ChampAll& rhs) {
						return ascending ? lhs.min.purchased < rhs.min.purchased
							: rhs.min.purchased < lhs.min.purchased;
						});
					break;
				default:;
				}
				sortSpecs->SpecsDirty = false;
			}
		}
	}

	static std::string FormatPurchaseDate(const std::string& purchased)
	{
		try
		{
			int64_t seconds = std::stoll(purchased);
			seconds /= 1000;
			char buffer[50];
			strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&seconds));
			return buffer;
		}
		catch (std::invalid_argument)
		{
			return "-";
		}
		catch (std::out_of_range)
		{
			return "-";
		}
	}

	static constexpr int ColumnID_Name = 0;
	static constexpr int ColumnID_ChampionId = 1;
	static constexpr int ColumnID_PurchaseDate = 2;

	static inline bool bOnOpen = true;
	static inline std::vector<ChampAll> champions;
	static inline std::mutex championsMutex;
	static inline std::atomic<unsigned> champsCount{ 0 };
	static inline std::atomic<bool> loading{ false };
};
