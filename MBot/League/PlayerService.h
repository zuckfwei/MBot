#pragma once

#include <string>

namespace League
{
	class PlayerService
	{
	public:
		struct PlayerInfo
		{
			std::string raw;
			std::string formatted;
			std::string accountId;
			std::string summonerId;
			std::string puuid;
			std::string internalName;
			std::string gameName;
			std::string tagLine;
			bool parsed = false;
		};

		enum class Availability
		{
			Online,
			Mobile,
			Away,
			Offline
		};

		static PlayerInfo FindByName(std::string name);
		static PlayerInfo FindByPuuid(const std::string& puuid);
		static PlayerInfo FindBySummonerId(const std::string& summonerId);
		static PlayerInfo GetCurrentPlayer();

		static std::string InviteToLobby(const PlayerInfo& player);
		static std::string SendFriendRequest(const PlayerInfo& player);
		static std::string BlockPlayer(const PlayerInfo& player);

		static std::string SetStatus(const std::string& status);
		static std::string SetAvailability(Availability availability);
		static std::string SuspendChat();
		static std::string ResumeChat();
		static std::string SetInvisibleBanner();
		static std::string SetProfileIcon(int iconId);
		static std::string SetChatIcon(int iconId);
		static std::string SetBackgroundSkin(const std::string& skinId);
	};
}
