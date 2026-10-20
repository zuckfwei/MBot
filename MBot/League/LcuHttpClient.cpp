#include "LcuHttpClient.h"

#include <format>

#include "Utils.h"

void League::LcuHttpClient::Configure(const ClientInfo& clientInfo)
{
	const std::scoped_lock lock(mutex_);
	clientInfo_ = clientInfo;
	session_ = std::make_unique<cpr::Session>();
	session_->SetVerifySsl(false);
	session_->SetHeader(Utils::StringToHeader(clientInfo_.header));
}

bool League::LcuHttpClient::IsConfigured() const
{
	const std::scoped_lock lock(mutex_);
	return session_ && clientInfo_.port > 0;
}

cpr::Response League::LcuHttpClient::Request(const std::string& method, const std::string& endpoint,
	const std::string& body)
{
	const std::scoped_lock lock(mutex_);
	if (!session_ || clientInfo_.port <= 0)
		return {};

	session_->SetUrl(std::format("https://127.0.0.1:{}{}", clientInfo_.port,
		endpoint.starts_with('/') ? endpoint : "/" + endpoint));
	session_->SetBody(body);

	if (method == "GET")
		return session_->Get();
	if (method == "POST")
		return session_->Post();
	if (method == "PATCH")
		return session_->Patch();
	if (method == "DELETE")
		return session_->Delete();
	return {};
}
