#pragma once

#include "Definitions.h"
#include "Includes.h"
#include "Misc.h"
#include "Config.h"
#include "League/MultiAccountService.h"

#pragma warning(disable : 4996)

class MultiAccountTab
{
public:
	static void Render()
	{
		if (ImGui::BeginTabItem("Accounts"))
		{
			static std::vector<MultiAccount::StoredAccount> accounts;
			static int selectedIndex = -1;
			static bool firstRun = true;

			// reload once
			if (firstRun)
			{
				firstRun = false;
				MultiAccount::LoadAccounts();
			}

			auto workerStatus = MultiAccount::GetWorkerStatus();
			bool busy = workerStatus.running;

			ImGui::TextWrapped("Add account closes Riot Client and opens a fresh login. Sign in manually with 'Stay signed in' checked; the account is then saved for future launches.");
			ImGui::Separator();

			// --- list of accounts
			ImGui::BeginChild("##accounts", ImVec2(0, 160), true);
			accounts = MultiAccount::GetAccounts();
			for (int i = 0; i < static_cast<int>(accounts.size()); ++i)
			{
				const auto& acc = accounts[i];
				bool current = i == selectedIndex;
				std::string label = acc.username;
				bool hasSession = !acc.sessionYaml.empty()
					&& (acc.sessionYaml.find("refresh_token:") != std::string::npos
						|| acc.sessionYaml.find("refresh-token:") != std::string::npos);
				if (!hasSession) label += "  [no session - re-add]";
				if (ImGui::Selectable((label + "##acc" + std::to_string(i)).c_str(), current))
					selectedIndex = i;
			}
			if (accounts.empty()) ImGui::TextDisabled("No accounts saved yet. Click Add account to begin.");
			ImGui::EndChild();

			ImGui::Separator();

			// --- action buttons
			ImGui::BeginDisabled(busy);
			if (ImGui::Button("Add account"))
			{
				MultiAccount::StartAddAccountWorker();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(busy || selectedIndex < 0 || selectedIndex >= static_cast<int>(accounts.size()));
			if (ImGui::Button("Delete account"))
			{
				MultiAccount::DeleteAccount(accounts[selectedIndex].username);
				MultiAccount::SaveAccounts();
				selectedIndex = -1;
			}
			ImGui::EndDisabled();
			ImGui::BeginDisabled(busy || selectedIndex < 0 || selectedIndex >= static_cast<int>(accounts.size()));
			if (ImGui::Button("Launch LoL selected acc"))
			{
				if (selectedIndex >= 0 && selectedIndex < static_cast<int>(accounts.size()))
				{
					MultiAccount::StartLaunchWorker(accounts[selectedIndex].username, true);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Launch Riot Client selected acc"))
			{
				if (selectedIndex >= 0 && selectedIndex < static_cast<int>(accounts.size()))
				{
					MultiAccount::StartLaunchWorker(accounts[selectedIndex].username, false);
				}
			}
			ImGui::EndDisabled();
			ImGui::Separator();
			ImGui::BeginDisabled(busy);
			if (ImGui::Button("Sign out"))
			{
				MultiAccount::StartSignOutWorker();
			}
			ImGui::SameLine();
			ImGui::HelpMarker("Clears the local Riot Client session without invalidating the saved accounts. You can launch a saved account again later.");
			ImGui::EndDisabled();

			ImGui::Separator();

			// --- worker status text
			ImGui::TextWrapped("%s", workerStatus.message.c_str());
			if (workerStatus.running) ImGui::TextDisabled("working ...");

			ImGui::EndTabItem();
		}
	}
};
