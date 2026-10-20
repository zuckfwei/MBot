#include "RiotIdService.h"

#include <format>

#include <cpr/cpr.h>
#include <json/json.h>

#include "LCU.h"
#include "Utils.h"

namespace
{
	std::string RiotClientUrl(std::string endpoint)
	{
		if (endpoint.empty() || endpoint[0] != '/')
			endpoint.insert(0, "/");
		return std::format("https://127.0.0.1:{}{}", LCU::riot.port, endpoint);
	}
}

RiotId::Eligibility RiotIdService::GetRiotClientEligibility()
{
	RiotId::Eligibility eligibility;
	if (!LCU::IsProcessGood() || !LCU::SetCurrentClientRiotInfo())
		return eligibility;

	cpr::Session session;
	session.SetVerifySsl(false);
	session.SetTimeout(cpr::Timeout{ 3000 });
	session.SetHeader(Utils::StringToHeader(LCU::riot.header));
	session.SetUrl(RiotClientUrl("/player-account/aliases/v1/eligibility"));
	const auto response = session.Post();
	if (response.status_code == 200)
		eligibility = RiotId::ParseEligibilityResponse(response.text);
	return eligibility;
}

std::string RiotIdService::SaveAlias(const std::string& gameName, const std::string& tagLine)
{
	Json::Value body;
	body["gameName"] = gameName;
	body["tagLine"] = tagLine;
	return LCU::Request("POST", "/lol-summoner/v1/save-alias", body.toStyledString());
}
