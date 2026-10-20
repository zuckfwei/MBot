#pragma once

#include <string>

#include <json/json.h>

namespace JsonUtils
{
	bool Parse(const std::string& response, Json::Value& root, bool failIfExtra = false);
	std::string FormatOrRaw(const std::string& response);
}
