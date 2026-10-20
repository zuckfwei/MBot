#include "JsonUtils.h"

#include <memory>

namespace JsonUtils
{
	bool Parse(const std::string& response, Json::Value& root, const bool failIfExtra)
	{
		Json::CharReaderBuilder builder;
		builder["failIfExtra"] = failIfExtra;
		const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
		JSONCPP_STRING error;
		return reader->parse(response.data(), response.data() + response.size(), &root, &error);
	}

	std::string FormatOrRaw(const std::string& response)
	{
		Json::Value root;
		if (!Parse(response, root))
			return response;

		Json::StreamWriterBuilder writer;
		return Json::writeString(writer, root);
	}
}
