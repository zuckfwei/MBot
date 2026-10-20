#pragma once

#include <cstdint>
#include <string>

namespace RiotId
{
	constexpr int GameNameMaxChars = 16;
	constexpr int TagLineMaxChars = 5;

	enum class EligibilityStatus
	{
		Unknown,
		Eligible,
		Blocked
	};

	struct Eligibility
	{
		EligibilityStatus status = EligibilityStatus::Unknown;
		std::int64_t eligibleAfter = 0;
	};

	int CountUtf8Chars(const char* text);
	int ClampUtf8ToMaxChars(const char* text, int textLength, int maxChars);

	Eligibility ParseEligibilityResponse(const std::string& response);
	std::string FormatLocalDateTimeWithUtcOffset(std::int64_t timestamp);
}
