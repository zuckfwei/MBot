#include "ChampionLookup.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace
{
	size_t LevenshteinDistance(const std::string& left, const std::string& right)
	{
		const size_t leftLength = left.length();
		const size_t rightLength = right.length();
		std::vector<std::vector<size_t>> distance(leftLength + 1, std::vector<size_t>(rightLength + 1));

		for (size_t i = 0; i <= leftLength; i++)
			distance[i][0] = i;
		for (size_t j = 0; j <= rightLength; j++)
			distance[0][j] = j;

		for (size_t i = 1; i <= leftLength; i++)
		{
			for (size_t j = 1; j <= rightLength; j++)
			{
				const size_t cost = left[i - 1] == right[j - 1] ? 0 : 1;
				distance[i][j] = (std::min)({
					distance[i - 1][j] + 1,
					distance[i][j - 1] + 1,
					distance[i - 1][j - 1] + cost
					});

				if (i > 1 && j > 1 && left[i - 1] == right[j - 2] && left[i - 2] == right[j - 1])
					distance[i][j] = (std::min)(distance[i][j], distance[i - 2][j - 2] + cost);
			}
		}

		return distance[leftLength][rightLength];
	}
}

namespace ChampionLookup
{
	Result FindClosest(const std::vector<Champ>& champions, const std::string& query)
	{
		Result result;
		if (query.empty())
			return result;

		size_t bestDistance = std::numeric_limits<size_t>::max();
		for (const auto& champion : champions)
		{
			if (const size_t distance = LevenshteinDistance(champion.name, query); distance <= bestDistance)
			{
				bestDistance = distance;
				result.name = champion.name;
				result.id = std::to_string(champion.key);
			}
		}
		return result;
	}
}
