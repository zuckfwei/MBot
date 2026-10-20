#include "RiotId.h"

#include <cstdio>
#include <ctime>

#include <json/json.h>

#include "JsonUtils.h"

namespace RiotId
{
	int CountUtf8Chars(const char* text)
	{
		if (text == nullptr)
			return 0;

		int count = 0;
		for (int i = 0; text[i] != '\0'; count++)
		{
			const unsigned char c = static_cast<unsigned char>(text[i]);
			if ((c & 0x80) == 0)
				i += 1;
			else if ((c & 0xE0) == 0xC0 && text[i + 1] != '\0')
				i += 2;
			else if ((c & 0xF0) == 0xE0 && text[i + 1] != '\0' && text[i + 2] != '\0')
				i += 3;
			else if ((c & 0xF8) == 0xF0 && text[i + 1] != '\0' && text[i + 2] != '\0' && text[i + 3] != '\0')
				i += 4;
			else
				i += 1;
		}
		return count;
	}

	int ClampUtf8ToMaxChars(const char* text, const int textLength, const int maxChars)
	{
		if (text == nullptr)
			return 0;

		int charCount = 0;
		int byteIndex = 0;

		while (text[byteIndex] != '\0' && byteIndex < textLength && charCount < maxChars)
		{
			const unsigned char c = static_cast<unsigned char>(text[byteIndex]);
			int charSize = 1;

			if ((c & 0x80) == 0)
				charSize = 1;
			else if ((c & 0xE0) == 0xC0)
				charSize = 2;
			else if ((c & 0xF0) == 0xE0)
				charSize = 3;
			else if ((c & 0xF8) == 0xF0)
				charSize = 4;

			if (byteIndex + charSize > textLength)
				break;

			byteIndex += charSize;
			charCount++;
		}

		return byteIndex;
	}

	Eligibility ParseEligibilityResponse(const std::string& response)
	{
		Eligibility eligibility;
		Json::Value root;
		if (!JsonUtils::Parse(response, root, true))
			return eligibility;

		if (root.isBool())
		{
			eligibility.status = root.asBool() ? EligibilityStatus::Eligible : EligibilityStatus::Blocked;
			return eligibility;
		}
		if (!root.isObject())
			return eligibility;

		if (root["isSuccess"].isBool())
		{
			const Json::Value& code = root["errorCode"];
			if (!code.isNull() && !code.isString())
				return eligibility;
			const std::string errorCode = code.isString() ? code.asString() : "";
			if (root["isSuccess"].asBool() && (errorCode.empty() || errorCode == "no_error"))
				eligibility.status = EligibilityStatus::Eligible;
			else if (!root["isSuccess"].asBool() && errorCode == "name_change_forbidden")
				eligibility.status = EligibilityStatus::Blocked;
			else
				return eligibility;
		}
		else if (root["eligible"].isBool() && !root.isMember("errorCode"))
			eligibility.status = root["eligible"].asBool() ? EligibilityStatus::Eligible : EligibilityStatus::Blocked;
		else
			return eligibility;

		if (eligibility.status == EligibilityStatus::Blocked)
		{
			Json::Value date = root["eligibleAfter"];
			if ((!date.isInt64() || date.asInt64() <= 0) && root["reason"].isObject())
				date = root["reason"]["free_change_cooldown_until"];
			if (date.isInt64() && date.asInt64() > 0)
				eligibility.eligibleAfter = date.asInt64();
		}
		return eligibility;
	}

	std::string FormatLocalDateTimeWithUtcOffset(const std::int64_t timestamp)
	{
		if (timestamp <= 0)
			return {};
		const std::time_t seconds = static_cast<std::time_t>(timestamp / 1000);
		std::tm localTime{};
		if (localtime_s(&localTime, &seconds) != 0)
			return {};

		std::tm utcTime{};
		if (gmtime_s(&utcTime, &seconds) != 0)
			return {};

		const int offsetMinutes = static_cast<int>(std::difftime(_mkgmtime(&localTime), _mkgmtime(&utcTime)) / 60);
		const char offsetSign = offsetMinutes >= 0 ? '+' : '-';
		const int absoluteOffsetMinutes = offsetMinutes >= 0 ? offsetMinutes : -offsetMinutes;
		const int offsetHours = absoluteOffsetMinutes / 60;
		const int offsetRemainderMinutes = absoluteOffsetMinutes % 60;

		char localDate[32]{};
		std::strftime(localDate, sizeof(localDate), "%d/%m/%Y %H:%M:%S", &localTime);

		char date[48]{};
		std::snprintf(date, sizeof(date), "%s UTC%c%02d:%02d", localDate, offsetSign, offsetHours, offsetRemainderMinutes);
		return date;
	}
}
