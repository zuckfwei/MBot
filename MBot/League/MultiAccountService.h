#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

// Multi-account support for the Riot Client, based on the "session file swap" mechanism:
//   - Per account we store the captured RiotGamesPrivateSettings.yaml (its
//     psl.authorization.riot-client block carries id_token + refresh_token + claims).
//   - Switching = kill Riot processes, clear credential-store leftovers, rewrite the
//     settings yaml with the saved session (keeping the LOCAL device tdid cookie),
//     relaunch RiotClientServices.exe and drive the product launch through the
//     client's own LCU (the same local HTTP call the Play button does).
// No external servers are contacted; everything besides the Riot auth servers happens
// on 127.0.0.1.

namespace MultiAccount
{
	struct StoredAccount
	{
		std::string username;    // "GameName#TagLine" (unique key)
		std::string puuid;       // from the captured id_token "sub" (best effort)
		std::string gameName;
		std::string tagLine;
		std::string sessionYaml; // raw captured RiotGamesPrivateSettings.yaml content
		long long   createdAt = 0;
	};

	// Port/token pair for an LCU endpoint (Riot Client Electron UI).
	struct LcuInfo
	{
		int port = 0;
		std::string token;   // base64 of "riot:<raw>" ready for "Authorization: Basic "
		bool Valid() const { return port > 0 && !token.empty(); }
	};

	struct WorkerStatus
	{
		bool running = false;
		bool success = false;
		std::string message;
	};

	/* ---- persistency (accounts.json next to MBot.exe) ---- */
	void LoadAccounts();
	void SaveAccounts();
	std::vector<StoredAccount> GetAccounts();
	bool DeleteAccount(const std::string& username);
	bool UpsertAccount(const StoredAccount& a);

	// discovery (uses Auth::GetClientInfo -- native PEB cmdline read)
	LcuInfo FindRiotClientLcu();    // "Riot Client.exe" (Electron UI)

	/* ---- process helpers ---- */
	void KillAllRiotProcesses();
	bool LaunchRiotClient(bool launchProduct);   // RCS with/without --launch-product
	bool ProcessExists(const wchar_t* exeName);

	/* ---- yaml session swap engine (audited port of the open-source mechanism) ---- */
	std::string SettingsYamlPath();              // %LOCALAPPDATA% Riot Client private settings
	bool WriteSessionYaml(const std::string& savedAccountYaml);  // merged [saved psl][local tdid]
	void ClearChromiumCookieStore();             // Data\Cookies + Data\Sessions
	int  ClearRiotCredentialManager();           // cmdkey /list + /delete (Riot entries)

	/* ---- local LCU helpers used by the launch chain ---- */
	bool WaitForClientLcu(LcuInfo& out, int timeoutMs, const std::function<void(const std::string&)>& progress);
	bool WaitPlayReady(const LcuInfo& info, int timeoutSec);     // /game-session/v1/ready == true
	std::string LaunchProduct(const LcuInfo& info, long* statusCode = nullptr); // POST /product-launcher/... (the Play call)
	std::string FetchRiotIdLive(const LcuInfo& info);            // /riot-client-auth/v1/userinfo -> GameName#Tag

	/* ---- long running workers ---- */
	// Launch the Riot Client, let the user log in, wait until the client persists the
	// session to its settings yaml, then store it. Does NOT kill the client afterwards.
	void StartAddAccountWorker();
	// Full chain for one account: swap session -> launch client -> drive the game launch.
	void StartLaunchWorker(const std::string& username, bool launchGame);
	// Close the client and clear ONLY the local session state (settings yaml + Electron
	// cookie store + credential-manager leftovers). SAVED accounts keep their captured
	// sessions: launching any of them jumps right back in. This is NOT the client's
	// logout button (that one invalidates the captured session server-side).
	void StartSignOutWorker();
	void CancelWorker();
	bool IsWorkerBusy();
	WorkerStatus GetWorkerStatus();
}
