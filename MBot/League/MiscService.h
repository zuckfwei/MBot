#pragma once

#include <string>
#include <vector>

class MiscService
{
public:
	struct FriendGroup
	{
		std::string name;
		int id = 0;
	};

	static std::string RestartUx();
	static std::string CloseClient();

	static std::string AcceptFriendRequests();
	static std::string DeleteFriendRequests();
	static std::vector<FriendGroup> GetFriendGroups();
	static std::string RemoveFriendsFromGroup(int groupId);

	static std::string SetMinimapScale(int minimapScale);
	static std::string DisenchantAll(const std::string& lootType, const std::string& lootLabel);
	static std::string RefundLastPurchase();
};
