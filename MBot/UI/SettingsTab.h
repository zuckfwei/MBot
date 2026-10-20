#pragma once

#include "Includes.h"
#include "Misc.h"
#include "Config.h"

class SettingsTab
{
public:
	static void Render()
	{
		if (ImGui::BeginTabItem("Settings"))
		{
			static std::string result;

			ImGui::Checkbox("Auto-rename", &S.autoRename);
			ImGui::SameLine();
			ImGui::HelpMarker("Automatically renames the program on launch");

			if (ImGui::Checkbox("Stream proof", &S.streamProof))
			{
				if (S.streamProof)
					SetWindowDisplayAffinity(S.hwnd, WDA_EXCLUDEFROMCAPTURE);
				else
					SetWindowDisplayAffinity(S.hwnd, WDA_NONE);
			}
			ImGui::SameLine();
			ImGui::HelpMarker("Hides the program in recordings and screenshots");

			if (ImGui::Button("Clean logs"))
			{
				if (MessageBoxA(nullptr, "Are you sure?", "Cleaning logs", MB_OKCANCEL) == IDOK)
				{
					result = Misc::ClearLogs();
				}
			}

			ImGui::SameLine();
			if (ImGui::Button("Force close client"))
				Misc::TaskKillLeague();

			static char bufLeaguePath[MAX_PATH];
			std::ranges::copy(S.leaguePath, bufLeaguePath);
			ImGui::Text("League path:");
			ImGui::InputText("##leaguePath", bufLeaguePath, MAX_PATH);
			S.leaguePath = bufLeaguePath;

			ImGui::SameLine();

			if (ImGui::Button("Try to auto detect path"))
			{
				using tSHGetFolderPathW = HRESULT(WINAPI*)(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPWSTR pszPath);
				const auto SHGetFolderPathW = reinterpret_cast<tSHGetFolderPathW>(GetProcAddress(LoadLibraryW(L"shell32.dll"), "SHGetFolderPathW"));

				TCHAR szPath[MAX_PATH];
				if (SUCCEEDED(SHGetFolderPathW(NULL, 0x23, NULL, 0, szPath)))
				{
					std::filesystem::path programData(szPath);
					auto productSettingPath = programData / "Riot Games\\Metadata\\league_of_legends.live\\league_of_legends.live.product_settings.yaml";
					if (std::filesystem::exists(productSettingPath))
					{
						std::ifstream fileStream(productSettingPath);
						if (fileStream.is_open())
						{
							std::string line;
							while (std::getline(fileStream, line))
							{
								if (line.contains("product_install_full_path: "))
								{
									if (std::size_t pos = line.find(":"); pos != std::string::npos)
									{
										std::string value = line.substr(pos + 2);
										value.erase(std::remove(value.begin(), value.end(), '\"'), value.end());
										size_t found;
										while ((found = value.find("\\\\")) != std::string::npos)
										{
											value.replace(found, 2, "/");
										}
										if (value.back() != '/')
											value += '/';
										S.leaguePath = value;
										std::fill(bufLeaguePath, bufLeaguePath + MAX_PATH, '\0');
										std::ranges::copy(S.leaguePath, bufLeaguePath);
									}
								}
							}
						}
					}
				}
			}

			ImGui::Separator();

			if (ImGui::Button("Reset window size"))
			{
				S.Window.width = 730;
				S.Window.height = 530;
				SetWindowPos(S.hwnd, nullptr, 0, 0, S.Window.width, S.Window.height, SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);
				Config::Save();
			}
			ImGui::SameLine();
			ImGui::Text(std::format("{0}x{1}", S.Window.width, S.Window.height).c_str());

			if (!result.empty())
				ImGui::Separator();
			ImGui::TextWrapped(result.c_str());

			ImGui::EndTabItem();
		}
	}
};
