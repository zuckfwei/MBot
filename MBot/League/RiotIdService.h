#pragma once

#include <string>

#include "RiotId.h"

class RiotIdService
{
public:
	static RiotId::Eligibility GetRiotClientEligibility();
	static std::string SaveAlias(const std::string& gameName, const std::string& tagLine);
};
