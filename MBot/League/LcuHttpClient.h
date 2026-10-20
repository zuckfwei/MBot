#pragma once

#include <memory>
#include <mutex>
#include <string>

#include <cpr/cpr.h>

#include "Auth.h"

namespace League
{
	class LcuHttpClient
	{
	public:
		void Configure(const ClientInfo& clientInfo);
		[[nodiscard]] bool IsConfigured() const;
		cpr::Response Request(const std::string& method, const std::string& endpoint,
			const std::string& body = "");

	private:
		mutable std::mutex mutex_;
		ClientInfo clientInfo_;
		std::unique_ptr<cpr::Session> session_;
	};
}
