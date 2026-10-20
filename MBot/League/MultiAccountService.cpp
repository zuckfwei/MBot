#include "MultiAccountService.h"

#include <chrono>
#include <format>
#include <fstream>
#include <thread>
#include <filesystem>
#include <sstream>
#include <atomic>
#include <mutex>
#include <ctime>

#include <cpr/cpr.h>
#include <json/json.h>
#include <tlhelp32.h>
#include <shlobj.h>

#include "Common/JsonUtils.h"
#include "Common/Config.h"
#include "Common/Utils.h"
#include "League/Auth.h"

namespace
{
	std::mutex g_mutex;
	std::vector<MultiAccount::StoredAccount> g_accounts;
	MultiAccount::WorkerStatus g_status;
	std::thread g_worker;
	std::atomic<bool> g_workerRunning{false};
	std::atomic<bool> g_workerCancel{false};

	// ------------------------------------------------------------------ misc helpers
	std::string AccountsFilePath()
	{
		wchar_t exePath[MAX_PATH] = { 0 };
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		std::filesystem::path p(exePath);
		return (p.parent_path() / "accounts.json").string();
	}

	void SetStatus(const std::string& message)
	{
		std::scoped_lock lock(g_mutex);
		g_status.message = message;
	}

	void FinishWorker(bool success, const std::string& message)
	{
		std::scoped_lock lock(g_mutex);
		g_status.running = false;
		g_status.success = success;
		g_status.message = message;
		g_workerRunning.store(false);
	}

	Json::Value AccountToJson(const MultiAccount::StoredAccount& a)
	{
		Json::Value root(Json::objectValue);
		root["username"] = a.username;
		root["puuid"] = a.puuid;
		root["gameName"] = a.gameName;
		root["tagLine"] = a.tagLine;
		root["sessionYaml"] = a.sessionYaml;
		root["createdAt"] = static_cast<Json::Int64>(a.createdAt);
		return root;
	}

	MultiAccount::StoredAccount AccountFromJson(const Json::Value& v)
	{
		MultiAccount::StoredAccount a;
		a.username = v.get("username", "").asString();
		a.puuid = v.get("puuid", "").asString();
		a.gameName = v.get("gameName", "").asString();
		a.tagLine = v.get("tagLine", "").asString();
		a.sessionYaml = v.get("sessionYaml", "").asString();
		a.createdAt = v.get("createdAt", 0).asInt64();
		return a;
	}

	bool ProcessExistsImpl(const wchar_t* processName)
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE) return false;
		bool found = false;
		PROCESSENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		if (Process32FirstW(snapshot, &entry))
		{
			do
			{
				if (_wcsicmp(entry.szExeFile, processName) == 0) { found = true; break; }
			} while (Process32NextW(snapshot, &entry));
		}
		CloseHandle(snapshot);
		return found;
	}

	bool KillProcessByName(const wchar_t* processName)
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE) return false;
		bool killedAnything = false;
		PROCESSENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		if (Process32FirstW(snapshot, &entry))
		{
			do
			{
				if (_wcsicmp(entry.szExeFile, processName) != 0) continue;
				HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
				if (!hProcess) continue;
				if (TerminateProcess(hProcess, 0)) killedAnything = true;
				CloseHandle(hProcess);
			} while (Process32NextW(snapshot, &entry));
		}
		CloseHandle(snapshot);
		return killedAnything;
	}

	std::string Base64Encode(const std::string& str)
	{
		static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		out.reserve((str.size() + 2) / 3 * 4);
		int val = 0, valb = -6;
		for (unsigned char c : str)
		{
			val = (val << 8) + c;
			valb += 8;
			while (valb >= 0)
			{
				out.push_back(table[(val >> valb) & 0x3F]);
				valb -= 6;
			}
		}
		if (valb > -6) out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
		while (out.size() % 4) out.push_back('=');
		return out;
	}

	// find "key":"value" in a decoded json-ish string
	std::string ExtractJsonString(const std::string& src, const std::string& key)
	{
		const std::string needle = "\"" + key + "\":\"";
		size_t p = src.find(needle);
		if (p == std::string::npos) return {};
		p += needle.size();
		size_t e = src.find('"', p);
		if (e == std::string::npos) return {};
		return src.substr(p, e - p);
	}

	std::string DecodeJwtPayload(const std::string& jwt)
	{
		size_t p1 = jwt.find('.');
		if (p1 == std::string::npos) return {};
		size_t p2 = jwt.find('.', p1 + 1);
		if (p2 == std::string::npos) return {};
		if (p2 <= p1 + 1) return {};
		std::string payload = jwt.substr(p1 + 1, p2 - p1 - 1);
		if (payload.size() % 4) payload.append(4 - payload.size() % 4, '=');
		for (char& c : payload) { if (c == '-') c = '+'; if (c == '_') c = '/'; }
		static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		unsigned val = 0; int bits = -8;
		for (char c : payload)
		{
			if (c == '=') break;
			const char* p = strchr(T, c);
			if (!p) continue;
			val = (val << 6) + static_cast<int>(p - T);
			bits += 6;
			if (bits >= 0) { out.push_back(char((val >> bits) & 0xff)); bits -= 8; }
		}
		return out;
	}

	cpr::Header DefaultLcuHeader(const MultiAccount::LcuInfo& info)
	{
		return cpr::Header{
			{"Authorization", "Basic " + info.token},
			{"Accept", "application/json"},
			{"Content-Type", "application/json"}
		};
	}

	// ------------------------------------------------------------------ yaml helpers
	std::string ReadTextFile(const std::filesystem::path& p)
	{
		std::ifstream f(p, std::ios::binary);
		if (!f.good()) return {};
		std::ostringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}

	bool WriteTextFile(const std::filesystem::path& p, const std::string& data)
	{
		std::error_code ec;
		std::filesystem::create_directories(p.parent_path(), ec);
		std::ofstream f(p, std::ios::binary | std::ios::trunc);
		if (!f.good()) return false;
		f << data;
		return f.good();
	}

	// Extract a top-level yaml section starting at "^<key>:" until the next non-indented key.
	std::string ExtractTopLevelSection(const std::string& yaml, const std::string& key)
	{
		std::istringstream in(yaml);
		std::string line;
		std::string out;
		bool capturing = false;
		while (std::getline(in, line))
		{
			bool isTopLevel = !line.empty() && line[0] != ' ' && line[0] != '\t';
			if (isTopLevel)
			{
				bool isHeader = line.rfind(key + ":", 0) == 0;
				if (isHeader) { capturing = true; out += line + "\n"; continue; }
				if (capturing) break;   // next top-level key reached
			}
			if (capturing) out += line + "\n";
		}
		return out;
	}

	// Pull the "value:" field out of a rso-authenticator tdid section (the LOCAL device cookie).
	std::string ExtractTdidValue(const std::string& rsoSection)
	{
		std::istringstream in(rsoSection);
		std::string line;
		while (std::getline(in, line))
		{
			if (line.find("value:") == std::string::npos) continue;
			size_t a = line.find('"');
			size_t b = line.rfind('"');
			if (a != std::string::npos && b != std::string::npos && b > a)
				return line.substr(a + 1, b - a - 1);
		}
		return {};
	}

	// Client-persisted live sessions carry a cookies list with the device tdid INSIDE the
	// session block. Cold/legacy captures may lack it; inject the local tdid there.
	std::string InjectSessionCookies(const std::string& loginSection, const std::string& tdidValue)
	{
		if (tdidValue.empty()) return loginSection;
		if (loginSection.find("cookies:") != std::string::npos) return loginSection;
		const std::string anchor = "session:\n";
		size_t pos = loginSection.find(anchor);
		if (pos == std::string::npos) return loginSection;
		size_t insert = pos + anchor.length();
		std::ostringstream cookie;
		cookie << "            cookies:\n"
		       << "            -   domain: \"riotgames.com\"\n"
		       << "                hostOnly: false\n"
		       << "                httpOnly: true\n"
		       << "                name: \"tdid\"\n"
		       << "                path: \"/\"\n"
		       << "                persistent: true\n"
		       << "                secureOnly: true\n"
		       << "                value: \"" << tdidValue << "\"\n";
		return loginSection.substr(0, insert) + cookie.str() + loginSection.substr(insert);
	}
}

namespace MultiAccount
{
	// ------------------------------------------------------------------ persistency
	void LoadAccounts()
	{
		std::scoped_lock lock(g_mutex);
		g_accounts.clear();
		std::ifstream f(AccountsFilePath());
		if (!f.good()) return;
		std::stringstream buffer;
		buffer << f.rdbuf();
		Json::Value root;
		if (!JsonUtils::Parse(buffer.str(), root) || !root.isArray()) return;
		for (const auto& item : root)
			g_accounts.push_back(AccountFromJson(item));
	}

	void SaveAccounts()
	{
		Json::Value root(Json::arrayValue);
		{
			std::scoped_lock lock(g_mutex);
			for (const auto& a : g_accounts)
				root.append(AccountToJson(a));
		}
		Json::StreamWriterBuilder writer;
		std::ofstream f(AccountsFilePath(), std::ios::trunc);
		f << Json::writeString(writer, root);
	}

	std::vector<StoredAccount> GetAccounts()
	{
		std::scoped_lock lock(g_mutex);
		return g_accounts;
	}

	bool UpsertAccount(const StoredAccount& input)
	{
		std::scoped_lock lock(g_mutex);
		for (auto& a : g_accounts)
		{
			if (!a.username.empty() && a.username == input.username)
			{
				a = input;
				return true;
			}
		}
		g_accounts.push_back(input);
		return true;
	}

	bool DeleteAccount(const std::string& username)
	{
		std::scoped_lock lock(g_mutex);
		for (auto it = g_accounts.begin(); it != g_accounts.end(); ++it)
		{
			if (it->username == username)
			{
				g_accounts.erase(it);
				return true;
			}
		}
		return false;
	}

	// ------------------------------------------------------------------ processes
	void KillAllRiotProcesses()
	{
		const wchar_t* procs[] = {
			L"LeagueClientUxRender.exe", L"LeagueClientUx.exe", L"LeagueClient.exe",
			L"LeagueCrashHandler64.exe", L"LeagueCrashHandler.exe",
			L"RiotClientUxRender.exe", L"RiotClientUx.exe", L"Riot Client.exe",
			L"RiotClientElectron.exe", L"RiotClientHelper.exe", L"RiotClientCrashHandler.exe",
			L"RiotClientServices.exe",
		};
		for (const wchar_t* name : procs)
		{
			for (int attempt = 0; attempt < 8; ++attempt)
			{
				if (!KillProcessByName(name)) break;
				std::this_thread::sleep_for(std::chrono::milliseconds(600));
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1200));
	}

	bool ProcessExists(const wchar_t* exeName)
	{
		return ProcessExistsImpl(exeName);
	}

	bool ShellStart(const std::wstring& exe, const std::wstring& args)
	{
		SHELLEXECUTEINFOW sei{ sizeof(sei) };
		sei.lpVerb = L"open";
		sei.lpFile = exe.c_str();
		sei.lpParameters = args.empty() ? nullptr : args.c_str();
		sei.nShow = SW_SHOWNORMAL;
		sei.fMask = SEE_MASK_NOCLOSEPROCESS;
		if (!ShellExecuteExW(&sei)) return false;
		if (sei.hProcess) CloseHandle(sei.hProcess);
		return true;
	}

	bool LaunchRiotClient(bool launchProduct)
	{
		std::filesystem::path rc = L"C:/Riot Games/Riot Client/RiotClientServices.exe";
		if (!std::filesystem::exists(rc))
			rc = L"C:/Program Files/Riot Games/Riot Client/RiotClientServices.exe";
		if (!std::filesystem::exists(rc)) return false;
		return ShellStart(rc.wstring(), launchProduct
			? L"--launch-product=league_of_legends --launch-patchline=live"
			: L"");
	}

	// ------------------------------------------------------------------ LCU discovery via Auth::GetClientInfo (PEB)
	LcuInfo FindRiotClientLcu()
	{
		// The Electron UI ("Riot Client.exe") carries --app-port/--remoting-auth-token.
		for (DWORD pid : Auth::GetAllProcessIds(L"Riot Client.exe"))
		{
			const auto info = Auth::GetClientInfo(pid, /*riotClient=*/false);
			if (info.port > 0 && !info.token.empty())
				return { info.port, info.token };
		}
		return {};
	}

	// ------------------------------------------------------------------ Riot API
	std::string FetchRiotIdLive(const LcuInfo& info)
	{
		if (!info.Valid()) return {};
		cpr::Session session;
		session.SetUrl(cpr::Url{std::format("https://127.0.0.1:{}/riot-client-auth/v1/userinfo", info.port)});
		session.SetHeader(DefaultLcuHeader(info));
		session.SetVerifySsl(false);
		session.SetTimeout(cpr::Timeout{6000});
		cpr::Response response = session.Get();
		if (response.status_code != 200) return {};
		const std::string name = ExtractJsonString(response.text, "game_name");
		const std::string tag = ExtractJsonString(response.text, "tag_line");
		if (name.empty()) return {};
		return tag.empty() ? name : name + "#" + tag;
	}

	// ------------------------------------------------------------------ yaml session swap engine
	std::string SettingsYamlPath()
	{
		PWSTR raw = nullptr;
		if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw) != S_OK) return {};
		std::filesystem::path dir(raw);
		CoTaskMemFree(raw);
		return (dir / L"Riot Games" / L"Riot Client" / L"Data" / L"RiotGamesPrivateSettings.yaml").string();
	}

	bool WriteSessionYaml(const std::string& savedAccountYaml)
	{
		const std::string current = ReadTextFile(SettingsYamlPath());
		const std::string savedRso = ExtractTopLevelSection(savedAccountYaml, "rso-authenticator"); // saved device tdid (fallback)
		std::string localRso = ExtractTopLevelSection(current, "rso-authenticator");                // LOCAL device tdid (must keep!)
		if (localRso.empty()) localRso = savedRso;

		// Current client builds persist the LIVE session inside psl.authorization.riot-client
		// (id_token + refresh_token + claims + scopes). Legacy builds used the riot-login block.
		const std::string savedPsl = ExtractTopLevelSection(savedAccountYaml, "psl");
		const bool pslHasSession = savedPsl.find("refresh_token:") != std::string::npos
			&& savedPsl.find("id_token:") != std::string::npos;
		const std::string loginLegacy = ExtractTopLevelSection(savedAccountYaml, "riot-login");

		std::string yaml;
		if (pslHasSession)
		{
			yaml = savedPsl;
		}
		else if (!loginLegacy.empty())
		{
			std::string tdid = ExtractTdidValue(localRso);
			yaml = InjectSessionCookies(loginLegacy, tdid);
		}
		else
		{
			return false;   // saved capture has no usable session
		}
		if (!localRso.empty()) yaml += "\n" + localRso;
		return WriteTextFile(SettingsYamlPath(), yaml);
	}

	void ClearChromiumCookieStore()
	{
		PWSTR raw = nullptr;
		if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw) != S_OK) return;
		std::filesystem::path data(raw);
		CoTaskMemFree(raw);
		data /= L"Riot Games"; data /= L"Riot Client"; data /= L"Data";
		std::error_code ec;
		for (const wchar_t* sub : { L"Cookies", L"Sessions" })
		{
			std::filesystem::path p = data / sub;
			if (std::filesystem::exists(p, ec))
				std::filesystem::remove_all(p, ec);
		}
	}

	int ClearRiotCredentialManager()
	{
		SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
		HANDLE rd = nullptr, wr = nullptr;
		if (!CreatePipe(&rd, &wr, &sa, 0)) return 0;
		SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOA si{};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = wr;
		si.hStdError = wr;
		PROCESS_INFORMATION pi{};
		if (!CreateProcessA(nullptr, (LPSTR)"cmdkey /list", nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
		{
			CloseHandle(wr); CloseHandle(rd);
			return 0;
		}
		CloseHandle(wr);
		WaitForSingleObject(pi.hProcess, 15000);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		std::string out;
		char buf[4096];
		DWORD got = 0;
		while (ReadFile(rd, buf, sizeof(buf) - 1, &got, nullptr) && got)
		{
			out.append(buf, got);
			if (out.size() > (1u << 20)) break;
		}
		CloseHandle(rd);

		int removed = 0;
		size_t pos = 0;
		while ((pos = out.find("Target:", pos)) != std::string::npos)
		{
			size_t end = out.find('\n', pos);
			if (end == std::string::npos) end = out.size();
			std::string line = out.substr(pos + 7, end - pos - 7);
			size_t a = line.find_first_not_of(" \t\r");
			size_t b = line.find_last_not_of(" \t\r");
			if (a != std::string::npos)
			{
				std::string target = line.substr(a, b - a + 1);
				std::string low = target;
				for (char& c : low) c = (char)tolower((unsigned char)c);
				if (low.find("riot") != std::string::npos || low.find("legion") != std::string::npos)
				{
					std::string cmd = "cmdkey /delete:\"" + target + "\"";
					STARTUPINFOA s2{};
					s2.cb = sizeof(s2);
					s2.dwFlags = STARTF_USESHOWWINDOW;
					s2.wShowWindow = SW_HIDE;
					PROCESS_INFORMATION p2{};
					if (CreateProcessA(nullptr, (LPSTR)cmd.c_str(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &s2, &p2))
					{
						WaitForSingleObject(p2.hProcess, 10000);
						CloseHandle(p2.hThread);
						CloseHandle(p2.hProcess);
						++removed;
					}
				}
			}
			pos = end;
		}
		return removed;
	}

	// ------------------------------------------------------------------ launch chain helpers
	bool WaitForClientLcu(LcuInfo& out, int timeoutMs, const std::function<void(const std::string&)>& progress)
	{
		auto start = std::chrono::steady_clock::now();
		int lastPct = -1;
		while (true)
		{
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - start).count();
			if (elapsed >= timeoutMs || g_workerCancel.load()) return false;
			out = FindRiotClientLcu();
			if (out.Valid()) return true;
			if (progress)
			{
				int pct = static_cast<int>(elapsed / 2000);
				if (pct != lastPct)
				{
					lastPct = pct;
					progress(std::format("waiting for the client LCU ({}s/{}s)", elapsed / 1000, timeoutMs / 1000));
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
	}

	bool WaitPlayReady(const LcuInfo& info, int timeoutSec)
	{
		// The Play button only fires when the client finished initializing:
		// /game-session/v1/ready -> true. Keep the check short so Play is not left waiting.
		auto start = std::chrono::steady_clock::now();
		while (!g_workerCancel.load())
		{
			if (std::chrono::steady_clock::now() - start >= std::chrono::seconds(timeoutSec))
				return false;
			cpr::Session session;
			session.SetUrl(cpr::Url{std::format("https://127.0.0.1:{}/game-session/v1/ready", info.port)});
			session.SetHeader(DefaultLcuHeader(info));
			session.SetVerifySsl(false);
			session.SetTimeout(cpr::Timeout{750});
			cpr::Response response = session.Get();
			if (response.status_code == 200 && response.text.compare(0, 4, "true") == 0)
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}
		return false;
	}

	std::string LaunchProduct(const LcuInfo& info, long* statusCode)
	{
		// THE Play-button call (hidden plugin; not listed in the client swagger dump):
		// POST /product-launcher/v1/products/{product}/patchlines/{patchline}
		// Long timeout: a real launch keeps the request busy for ~10s; a client-side
		// timeout here would make us re-POST while the first one is still working.
		cpr::Session session;
		session.SetUrl(cpr::Url{std::format(
			"https://127.0.0.1:{}/product-launcher/v1/products/league_of_legends/patchlines/live", info.port)});
		session.SetHeader(DefaultLcuHeader(info));
		session.SetVerifySsl(false);
		session.SetTimeout(cpr::Timeout{15000});
		session.SetBody("{}");
		cpr::Response response = session.Post();
		if (statusCode) *statusCode = response.status_code;
		return std::format("HTTP {} {}", response.status_code, response.text.substr(0, 200));
	}

	// ------------------------------------------------------------------ workers
	void StartAddAccountWorker()
	{
		if (g_workerRunning.load()) return;

		auto body = []()
		{
			auto start = std::chrono::steady_clock::now();
			auto elapsedSec = [&]
			{
				return std::chrono::duration_cast<std::chrono::seconds>(
					std::chrono::steady_clock::now() - start).count();
			};

			// A previous local login must not be mistaken for the new account.
			// Clear only local state; captured sessions in accounts.json remain intact.
			SetStatus("[Add] preparing a fresh local login ...");
			KillAllRiotProcesses();
			const std::string settingsPath = SettingsYamlPath();
			if (settingsPath.empty() || !WriteTextFile(settingsPath,
				"psl:\n    authorization:\n        riot-client: null\n"))
			{
				FinishWorker(false, "[Add] FAIL: could not clear the local Riot Client session.");
				return;
			}
			ClearChromiumCookieStore();
			ClearRiotCredentialManager();
			SetStatus("[Add] launching Riot Client (log there when it opens) ...");
			if (!LaunchRiotClient(false))
			{
				FinishWorker(false, "[Add] FAIL: could not launch RiotClientServices.exe. Check the install path.");
				return;
			}

			LcuInfo info;
			if (!WaitForClientLcu(info, 120000, [&](const std::string& msg)
				{
					SetStatus(std::format("[Add] {}", msg));
				}))
			{
				FinishWorker(false, "[Add] FAIL: client LCU not detected in 2 min (PEB/cmdline read blocked?).");
				return;
			}
			SetStatus(std::format("[Add] client LCU ready (port {}). Log into the account NOW with 'stay signed in' checked.", info.port));

			// Poll until the client persists the session into its settings yaml
			// (psl.authorization.riot-client with refresh_token), then capture it.
			std::string yaml;
			std::string riotId;
			while (!g_workerCancel.load())
			{
				std::this_thread::sleep_for(std::chrono::seconds(2));
				yaml = ReadTextFile(SettingsYamlPath());
				const bool hasSession = yaml.find("refresh_token:") != std::string::npos
					|| yaml.find("refresh-token:") != std::string::npos;

				if (riotId.empty())
				{
					riotId = FetchRiotIdLive(info);
					if (!riotId.empty())
						SetStatus(std::format("[Add] logged in as {} -- waiting for the client to persist the session ...", riotId));
				}
				SetStatus(std::format("[Add] waiting for the client to persist the session ({}s elapsed) ...", elapsedSec()));
				if (hasSession)
					break;
				if (elapsedSec() > 900)
				{
					FinishWorker(false, "[Add] TIMEOUT: the client never persisted a session. Be sure 'stay signed in' was checked and the account shows as logged in.");
					return;
				}
			}

			StoredAccount account;
			account.username = riotId.empty() ? "[unnamed]" : riotId;
			if (!account.username.empty())
			{
				size_t hash = account.username.find('#');
				if (hash != std::string::npos)
				{
					account.gameName = account.username.substr(0, hash);
					account.tagLine = account.username.substr(hash + 1);
				}
				else
				{
					account.gameName = account.username;
				}
			}
			account.sessionYaml = yaml;
			account.createdAt = static_cast<long long>(std::time(nullptr));

			// puuid from the persisted id_token (psl block), best effort
			const std::string psl = ExtractTopLevelSection(yaml, "psl");
			size_t it = psl.find("id_token: \"");
			if (it != std::string::npos)
			{
				size_t b = psl.find('"', it + 11);
				std::string idToken = psl.substr(it + 11, b - (it + 11));
				account.puuid = ExtractJsonString(DecodeJwtPayload(idToken), "sub");
			}

			UpsertAccount(account);
			SaveAccounts();
			FinishWorker(true, std::format("[Add] account '{}' captured with live session. It is ready for the direct-launch.", account.username));
		};

		if (g_worker.joinable()) g_worker.join();
		g_workerRunning.store(true);
		g_workerCancel.store(false);
		{
			std::scoped_lock lock(g_mutex);
			g_status = WorkerStatus{};
			g_status.running = true;
		}
		g_worker = std::thread(body);
		g_worker.detach();
	}

	void StartLaunchWorker(const std::string& username, bool launchGame)
	{
		if (g_workerRunning.load()) return;
		std::optional<StoredAccount> chosen;
		{
			std::scoped_lock lock(g_mutex);
			for (auto& a : g_accounts)
				if (a.username == username) { chosen = a; break; }
		}
		if (!chosen)
		{
			std::scoped_lock lock(g_mutex);
			g_status.message = "[Launch] account not found: " + username;
			return;
		}
		const auto account = *chosen;

		auto body = [account, launchGame]()
		{
			auto start = std::chrono::steady_clock::now();
			auto elapsedSec = [&]
			{
				return std::chrono::duration_cast<std::chrono::seconds>(
					std::chrono::steady_clock::now() - start).count();
			};

			SetStatus(std::format("[Launch:{}] step 1/5: killing all Riot processes ...", account.username));
			KillAllRiotProcesses();

			SetStatus(std::format("[Launch:{}] step 2/5: clearing stale credentials/cookies ...", account.username));
			ClearChromiumCookieStore();
			int removed = ClearRiotCredentialManager();
			SetStatus(std::format("[Launch:{}] {} credential entries removed", account.username, removed));

			SetStatus(std::format("[Launch:{}] step 3/5: writing saved session into the client settings ...", account.username));
			if (!WriteSessionYaml(account.sessionYaml))
			{
				FinishWorker(false, std::format("[Launch:{}] FAIL: saved capture has no usable session. Re-add this account.", account.username));
				return;
			}

			SetStatus(std::format("[Launch:{}] step 4/5: starting Riot Client ...", account.username));
			if (!LaunchRiotClient(launchGame))
			{
				FinishWorker(false, std::format("[Launch:{}] FAIL: could not start RiotClientServices.exe.", account.username));
				return;
			}

			LcuInfo info;
			if (!WaitForClientLcu(info, 240000, [&](const std::string& msg)
				{
					SetStatus(std::format("[Launch:{}] {}", account.username, msg));
				}))
			{
				FinishWorker(false, std::format("[Launch:{}] FAIL: client LCU not detected in 4 min.", account.username));
				return;
			}

			if (!launchGame)
			{
				SetStatus(std::format("[Launch:{}] session swapped; client is starting as '{}' (no game launch requested).", account.username, account.username));
				FinishWorker(true, std::format("[Launch:{}] client relaunched with the account session.", account.username));
				return;
			}

			SetStatus(std::format("[Launch:{}] waiting for the Play-ready state (game-session/v1/ready) ...", account.username));
			WaitPlayReady(info, 180);

			// Keep re-triggering the Play call UNTIL the game process appears; the moment
			// LeagueClient.exe is up we stop POSTing (your "simplest version" behavior).
			SetStatus(std::format("[Launch:{}] step 5/5: triggering Play until the game opens ...", account.username));
			auto launchStart = std::chrono::steady_clock::now();
			std::string result;
			long statusCode = 0;
			bool spawned = false;
			while (!g_workerCancel.load())
			{
				if (ProcessExists(L"LeagueClient.exe")) { spawned = true; break; }
				if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - launchStart).count() > 300)
					break;

				result = LaunchProduct(info, &statusCode);
				SetStatus(std::format("[Launch:{}] Play -> {} | watching for the game process ...", account.username, result));

				// after an accepted call give the client a longer window before re-POSTing
				const int watchSec = (statusCode >= 200 && statusCode < 300) ? 15 : 5;
				auto wake = std::chrono::steady_clock::now() + std::chrono::seconds(watchSec);
				while (!g_workerCancel.load() && std::chrono::steady_clock::now() < wake)
				{
					if (ProcessExists(L"LeagueClient.exe")) { spawned = true; break; }
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
				}
				if (spawned) break;
			}
			if (!spawned && !ProcessExists(L"LeagueClient.exe"))
			{
				FinishWorker(false, std::format("[Launch:{}] the game did not open within 5 min (last: {}). Check the client window for a prompt.", account.username, result));
				return;
			}
			FinishWorker(true, std::format("[Launch:{}] *** LoL IS LAUNCHING ({:.0f}s total) ***", account.username, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()));
		};

		if (g_worker.joinable()) g_worker.join();
		g_workerRunning.store(true);
		g_workerCancel.store(false);
		{
			std::scoped_lock lock(g_mutex);
			g_status = WorkerStatus{};
			g_status.running = true;
		}
		g_worker = std::thread(body);
		g_worker.detach();
	}

	void CancelWorker()
	{
		g_workerCancel.store(true);
	}

	void StartSignOutWorker()
	{
		if (g_workerRunning.load()) return;

		auto body = []()
		{
			SetStatus("[SignOut] closing Riot processes ...");
			KillAllRiotProcesses();

			SetStatus("[SignOut] clearing the LOCAL session only (saved accounts untouched) ...");
			// Empty session state, same shape the client itself writes when logged out.
			const std::string settingsPath = SettingsYamlPath();
			if (settingsPath.empty() || !WriteTextFile(settingsPath,
				"psl:\n    authorization:\n        riot-client: null\n"))
			{
				FinishWorker(false, "[SignOut] FAIL: could not clear the local Riot Client session.");
				return;
			}
			ClearChromiumCookieStore();
			ClearRiotCredentialManager();

			SetStatus("[SignOut] reopening the client on the login screen ...");
			if (!LaunchRiotClient(false))
			{
				FinishWorker(false, "[SignOut] local session cleared, but Riot Client could not be reopened.");
				return;
			}

			FinishWorker(true, "[SignOut] signed out locally. Saved accounts are intact -- launching any of them jumps straight back in.");
		};

		if (g_worker.joinable()) g_worker.join();
		g_workerRunning.store(true);
		g_workerCancel.store(false);
		{
			std::scoped_lock lock(g_mutex);
			g_status = WorkerStatus{};
			g_status.running = true;
		}
		g_worker = std::thread(body);
		g_worker.detach();
	}

	bool IsWorkerBusy()
	{
		return g_workerRunning.load();
	}

	WorkerStatus GetWorkerStatus()
	{
		std::scoped_lock lock(g_mutex);
		return g_status;
	}
}
