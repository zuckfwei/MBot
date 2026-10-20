#pragma once

#include <string>
#include <vector>

#include "Definitions.h"

namespace ChampionLookup
{
	struct Result
	{
		std::string name;
		std::string id;
	};

	Result FindClosest(const std::vector<Champ>& champions, const std::string& query);
}
