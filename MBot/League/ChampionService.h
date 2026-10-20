#pragma once

#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "Definitions.h"

namespace League::ChampionService
{
	std::vector<std::pair<int, std::string>> ParseOwnedChampions(const std::string& response);
	std::vector<std::pair<int, std::string>> GetOwnedChampions();
	// non-blocking reads for the UI: cached owned champions, refreshed in the background
	std::vector<std::pair<int, std::string>> GetOwnedChampionsCached();
	void PrefetchOwnedChampions();
	std::string GetCurrentPatch();
	void LoadChampionSkins();
	std::vector<ChampAll> GetChampionCollection();
	std::vector<Skin> GetOwnedSkins();
}
