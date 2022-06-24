// NorthstarUpdater.cpp : This file contains the 'main' function. Program execution begins and ends there.
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#define CURL_STATIC_LIB
#include "libcurl/include/curl/curl.h"
#include "fmt/core.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <string>
#include <cstdio>
#include <Windows.h>
#include <Psapi.h>
#include <set>
#include <map>
#include <process.h>
#include <Tlhelp32.h>
#include <winbase.h>
#include <math.h>
#include <comdef.h>
#include <filesystem>
#include <sstream>
#include "winldap.h"
#include "winsock2.h"
#include "include/nlohmann/json.hpp"
#include "include/semver.hpp"
#include <sys/stat.h>
#include "Shldisp.h"
#include "atlbase.h"
#include "include/openssl/core.h"
#include <codecvt>
#include <indicators/cursor_control.hpp>
#include <indicators/progress_bar.hpp>
#include <indicators/block_progress_bar.hpp>
#include <indicators/indeterminate_progress_bar.hpp>
#include <indicators/cursor_control.hpp>
#include <indicators/termcolor.hpp>
#include <fstream>
#pragma comment(lib, "include/libcurl/lib/libcurl_a.lib")
#pragma comment(lib, "version.lib")

#pragma comment(lib, "Normaliz.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Wldap32.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "advapi32.lib")
//std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
std::string g_TempFolderName = "UpdaterTemp";
std::string g_TempPackageName = "Latest.zip";
std::string g_NorthstarCustomDir = "R2Northstar\\mods\\Northstar.Custom";
std::string g_NorthstarServerDir = "R2Northstar\\mods\\Northstar.CustomServers";
std::string g_NorthstarClientDir = "R2Northstar\\mods\\Northstar.Client";
std::string g_NorthstarCNCustomDir = "R2Northstar\\mods\\NorthstarCN.Custom";

std::string f_DedicatedConfig = "R2northstar\\mods\\Northstar.CustomServers\\mod\\cfg\\autoexec_ns_server.cfg";
std::string f_ClientConfig = "R2northstar\\mods\\Northstar.Client\\mod\\cfg\\autoexec_ns_client.cfg";

std::string f_DedicatedConfigBackup = "UpdaterTemp\\autoexec_ns_server.cfg";
std::string f_ClientConfigBackup = "UpdaterTemp\\autoexec_ns_client.cfg";

std::string g_GnuUnzipURL = "https://updater-wolf109909.vercel.app/unzip.exe";
std::string g_MasterServerAddress = "nscn.wolf109909.top";
std::string g_MasterServerVersionEndPoint = "https://nscn.wolf109909.top/version/query";
bool g_ShouldPerformUpdate = false;
bool forceUpdate = false;
bool g_SuccessfullyFetchedLatestVersion = false;
bool g_ServerCfgFound = true;
bool g_CleanInstall = false;
bool g_ClientCfgFound = true;
std::string g_RemoteVersionString;
std::string g_LocalVersionString;
std::string g_LatestVersionURL;
int g_NorthstarPackageDownloadSize;

using namespace indicators;

int nb_bar;
double last_progress, progress_bar_adv;

HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

int legacy_progress_bar(void* bar, double t, double d)
{
	if (last_progress != round(d / t * 100))
	{
		nb_bar = 25;
		progress_bar_adv = round(d / t * nb_bar);

		std::cout << "\r ";
		SetConsoleTextAttribute(hConsole, 160);
		std::cout << " Progress : [ ";

		if (round(d / t * 100) < 10)
		{
			std::cout << "0" << round(d / t * 100) << " %]";
		}
		else
		{
			std::cout << round(d / t * 100) << " %] ";
		}

		SetConsoleTextAttribute(hConsole, 15);
		std::cout << " [";
		SetConsoleTextAttribute(hConsole, 10);
		for (int i = 0; i <= progress_bar_adv; i++)
		{
			std::cout << "#";
		}
		SetConsoleTextAttribute(hConsole, 15);
		for (int i = 0; i < nb_bar - progress_bar_adv; i++)
		{
			std::cout << ".";
		}

		std::cout << "]";
		last_progress = round(d / t * 100);
	}
	return 0;
}

void hidecursor()
{
	HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_CURSOR_INFO info;
	info.dwSize = 100;
	info.bVisible = FALSE;
	SetConsoleCursorInfo(consoleHandle, &info);
}

long GetFileSize(std::string filename)
{
	struct stat stat_buf;
	int rc = stat(filename.c_str(), &stat_buf);
	return rc == 0 ? stat_buf.st_size : -1;
}


int progress_func(BlockProgressBar *ptr, double TotalToDownload, double NowDownloaded, double TotalToUpload, double NowUploaded)
{
	// ensure that the file to be downloaded is not empty
	// because that would cause a division by zero error later on
	if (TotalToDownload <= 0.0)
		{
			return 0;
		}

	double fractiondownloaded = NowDownloaded / TotalToDownload;
	//std::cout << "[I] " << fractiondownloaded << std::endl;
	ptr->set_progress(fractiondownloaded * 100);
	return 0;
}

size_t write_data(void* ptr, size_t size, size_t nmemb, FILE* stream)
{
	size_t written = fwrite(ptr, size, nmemb, stream);
	return written;
}
bool IsPathExist(const std::string& s)
{
	struct stat buffer;
	return (stat(s.c_str(), &buffer) == 0);
}
void SetCommonHttpClientOptions(CURL* curl)
{
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, false);
}
size_t CurlWriteToStringBufferCallback(char* contents, size_t size, size_t nmemb, void* userp)
{
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}

void Parse(int result[4], const std::string& input)
{
	std::istringstream parser(input);
	parser >> result[0];
	for (int idx = 1; idx < 4; idx++)
	{
		parser.get(); // Skip period
		parser >> result[idx];
	}
}

bool LessThanVersion(const std::string& a, const std::string& b)
{
	int parsedA[4], parsedB[4];
	Parse(parsedA, a);
	Parse(parsedB, b);
	return std::lexicographical_compare(parsedA, parsedA + 4, parsedB, parsedB + 4);
}

int GetFileVersion(const char* filename, char* ver)
{
	DWORD dwHandle, sz = GetFileVersionInfoSizeA(filename, &dwHandle);
	if (0 == sz)
	{
		return 1;
	}
	char* buf = new char[sz];
	if (!GetFileVersionInfoA(filename, dwHandle, sz, &buf[0]))
	{
		delete[] buf;
		return 2;
	}
	VS_FIXEDFILEINFO* pvi;
	sz = sizeof(VS_FIXEDFILEINFO);
	if (!VerQueryValueA(&buf[0], "\\", (LPVOID*)&pvi, (unsigned int*)&sz))
	{
		delete[] buf;
		return 3;
	}
	sprintf(
		ver,
		"%d.%d.%d.%d",
		pvi->dwProductVersionMS >> 16,
		pvi->dwFileVersionMS & 0xFFFF,
		pvi->dwFileVersionLS >> 16,
		pvi->dwFileVersionLS & 0xFFFF);
	delete[] buf;
	return 0;
}
bool GetLocalNorthstarVersion() 
{
	if (!std::filesystem::exists("NorthstarLauncher.exe"))
	{
		return false;
	}
	char version[80];
	std::cout << "[*] Fetching Local NorthstarCN Version\n";
	
	GetFileVersion("NorthstarLauncher.exe", version);
	std::cout << "[*] Local Version Detected:" << version << std::endl;
	g_LocalVersionString = version;
	return true;
}

bool TryDownloadUnzip() 
{

	CURL* curl;
	FILE* fp;
	CURLcode res;
	curl = curl_easy_init();
	SetCommonHttpClientOptions(curl);

	if (curl)
	{
		//std::cout << std::filesystem::current_path() << std::endl;
		fp = fopen("unzip.exe", "wb");
		curl_easy_setopt(curl, CURLOPT_URL, g_GnuUnzipURL.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
		/* we tell libcurl to follow redirection */
		// Internal CURL progressmeter must be disabled if we provide our own callback
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, FALSE);
		// Install the callback function
		
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		
		// curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		std::cout << "[*] Downloading GNU unzip... \n" << std::endl;
		if (forceUpdate)
		{
			curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, legacy_progress_bar);
		}
		else
		{
			BlockProgressBar bar {
				option::BarWidth {80},
				option::Start {"["},
				option::End {"]"},
				option::ForegroundColor {Color::white},
				option::FontStyles {std::vector<FontStyle> {FontStyle::bold}}};

			curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, &bar);
			curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_func);
		}
		
		res = curl_easy_perform(curl);
		/* always cleanup */
		if (res != CURLE_OK)
		{
			fprintf(stderr, "[-] curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			curl_easy_cleanup(curl);
			fclose(fp);
			return false;
		}
		curl_easy_cleanup(curl);
		fclose(fp);
		std::cout << "[*] Download Success!\n" << std::endl;
		return true;
		std::cout << std::endl;//hey wolf are u here? bad bad
	}
	return false;


}



bool GetUnzipApplication() 
{
	std::cout << "[*] Checking GNU Unzip\n";
	
	if (!std::filesystem::exists("unzip.exe"))
	{
		return false;
	}
	if (GetFileSize("unzip.exe") < 10000)
	{
		return false;
	}
	return true;

}

bool CheckUnzipEnvironment() 
{
	if (!GetUnzipApplication())
	{
		std::cout << "[*] GNU unzip not found! Trying to download GNU unzip...\n";
		

		if (!TryDownloadUnzip())
		{
			std::cout << "[-] Failed to download GNU unzip!\n";
			return false;
		}
	}
	return true;
	std::cout << "[*] GNU unzip is successfully downloaded!\n";
}
bool GetLatestNorthstarVersion()
{

		std::string urljsonstr;
		std::cout << "[*] Fetching Latest NorthstarCN Version" << std::endl;

		CURL* curl = curl_easy_init();
		SetCommonHttpClientOptions(curl);
		std::string readBuffer;
		char* errBuffer;
		curl_easy_setopt(curl, CURLOPT_URL, g_MasterServerVersionEndPoint.c_str());
		//curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, &errBuffer);
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

		CURLcode result = curl_easy_perform(curl);
		//{"tag_name": "v1.8.2","assets": [{"browser_download_url": "http://180.76.180.29/CDN/Titanfall2/v1.8.2.zip"}]}
		if (result == CURLcode::CURLE_OK)
		{
			
			//std::cout << readBuffer << std::endl;
			auto reply = nlohmann::json::parse(readBuffer);
				
			g_RemoteVersionString = reply["tag_name"];
			g_LatestVersionURL = reply["assets"][0]["browser_download_url"];

			std::cout << "[*] Found latest version: " << g_RemoteVersionString << std::endl;
			//semver::version v1 {g_RemoteVersionString};
			curl_easy_cleanup(curl);
			return true;
		}
		else
		{
			return false;
		}

}

bool VerifyPackageIntegrity() 
{
	if (!std::filesystem::exists(g_TempFolderName + "\\" + g_TempPackageName))
	{
		return false;
	}
	int downloadedsize = std::filesystem::file_size(g_TempFolderName + "\\" + g_TempPackageName);
	
	std::cout << "[*] Verifying Package Integrity\n";
	std::cout << "[*] Size: " << g_NorthstarPackageDownloadSize << " / " << downloadedsize << std::endl;
	if (g_NorthstarPackageDownloadSize != downloadedsize)
	{
		std::cout << "[-] Package size mismatch!\n";
		return false;
	}
	if (downloadedsize < 100000)
	{
		std::cout << "[-] Package size is too small!\n";
		MessageBoxA(
			0,
			"更新北极星CN时服务器返回了错误的文件,请尝试重新执行更新操作。错误代码: 7" ,"NorthstarCN自动更新",
			MB_ICONERROR | MB_OK);
		return false;
	}

	std::cout << "[*] Packge size match!\n";
	return true;
	
	
}
bool downloadLatestRelease()
{
	CURL* curl;
	FILE* fp;
	CURLcode res;
	curl = curl_easy_init();
	SetCommonHttpClientOptions(curl);
	
	if (curl)
	{
		std::cout << "[*] Downloading : " << g_RemoteVersionString << std::endl;
		fp = fopen((g_TempFolderName + "\\" + g_TempPackageName).c_str(), "wb");
		curl_easy_setopt(curl, CURLOPT_URL, g_LatestVersionURL.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
		/* we tell libcurl to follow redirection */
		// Internal CURL progressmeter must be disabled if we provide our own callback
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, FALSE);
		// Install the callback function
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		
		
		if (forceUpdate)
		{
			curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, legacy_progress_bar);
		}
		else
		{
			BlockProgressBar bar {
				option::BarWidth {80},
				option::Start {"["},
				option::End {"]"},
				option::ForegroundColor {Color::white},
				option::FontStyles {std::vector<FontStyle> {FontStyle::bold}}};

			curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, &bar);
			curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_func); 
		}
		

		
		

		
		res = curl_easy_perform(curl);  
		curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &g_NorthstarPackageDownloadSize);
		//std::cout << g_NorthstarPackageDownloadSize << std::endl;
		if (g_NorthstarPackageDownloadSize < 10000)
		{
			std::cout << "[-] Empty server response!\n";
			curl_easy_cleanup(curl);
			return false;
		}
		/* always cleanup */
		if (res != CURLE_OK)
		{
			fprintf(stderr, "[-] curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			curl_easy_cleanup(curl);
			fclose(fp);
			return false;
		}
		curl_easy_cleanup(curl);
		fclose(fp);
		std::cout << std::endl;
		return true;
	}
	return false;
}

bool CheckAndGetLatestRelease() 
{
	if (!IsPathExist(g_TempFolderName))
	{
		std::cout << "[*] Can't find temp folder. Creating a new one" << std::endl;
		std::filesystem::create_directory(g_TempFolderName);
		

	}

	std::cout << "[*] Staring Download:" << g_LatestVersionURL << std::endl;
	//downloadUrl((g_TempFolderName + "/" + g_TempPackageName).c_str(), g_LatestVersionURL.c_str());
	if (!downloadLatestRelease())
	{
		return false;
	}
	
	return true;
}


bool UnpackLatestToTemp() 
{
	std::string sourcefile = g_TempFolderName + "\\" + g_TempPackageName;
	if (!std::filesystem::exists(sourcefile))
	{
		return false;
	}
	std::cout << "[*] Installing NorthstarCN..." << std::endl;
	// zip exists, so just drop it in temp folder.

	if (forceUpdate)
	{
		std::string command = "unzip -q -o " + sourcefile + " -x NorthstarUpdater.exe unzip.exe";
		system(command.c_str());
	
	}
	else
	{
		indicators::IndeterminateProgressBar bar {
			indicators::option::BarWidth {80},
			indicators::option::Start {"["},
			indicators::option::Fill {"·"},
			indicators::option::Lead {"<==>"},
			indicators::option::End {"]"},
			indicators::option::PostfixText {"Unpacking Release..."},
			indicators::option::ForegroundColor {indicators::Color::yellow},
			indicators::option::FontStyles {std::vector<indicators::FontStyle> {indicators::FontStyle::bold}}};

		auto job = [&bar, sourcefile]
		{
			std::string command = "unzip -qq -o " + sourcefile + " -x NorthstarUpdater.exe unzip.exe";
			system(command.c_str());
			bar.mark_as_completed();
			std::cout << termcolor::bold << termcolor::green << "Unpack complete!\n" << termcolor::reset;
		};

		std::thread unzip_thread(job);

		while (!bar.is_completed())
		{
			bar.tick();
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		unzip_thread.join();
	}
	

}
void CleanTempFiles() 
{
	std::cout << "[*] NorthstarCN installation succeeded." << std::endl;
	std::cout << "[*] Removing Temporary Files..." << std::endl;
	if (!IsPathExist(g_TempFolderName))
	{
		std::cout << "[-] There is no Temporary files.. huh, that's unusual." << std::endl;
	}
	else
	{
		std::filesystem::remove_all(g_TempFolderName);
	}
}
void BackupConfigFiles() 
{
	std::cout << "[*] Backing up configuration files..." << std::endl;
	if (!std::filesystem::exists(f_DedicatedConfig))
	{
		std::cout << "[*] Could not find server configuration file!" << std::endl;
		g_ServerCfgFound = false;
	}
	if (!std::filesystem::exists(f_ClientConfig))
	{
		std::cout << "[*] Could not find client configuration file!" << std::endl;
		g_ClientCfgFound = false;
	}
	if (!g_ServerCfgFound && !g_ClientCfgFound)
	{
		std::cout << "[*] Could find any files to backup!" << std::endl;
		std::cout << "[*] Skipping configuration backup" << std::endl;
		return;
	}

	if (g_ServerCfgFound)
	{
		std::cout << "[*] Backing up server configuration..." << std::endl;
		std::filesystem::copy_file(f_DedicatedConfig, f_DedicatedConfigBackup, std::filesystem::copy_options::overwrite_existing);
	}
	if (g_ClientCfgFound)
	{
		std::cout << "[*] Backing up client configuration..." << std::endl;
		std::filesystem::copy_file(f_ClientConfig, f_ClientConfigBackup, std::filesystem::copy_options::overwrite_existing);
	}
	
}
void RestoreConfigFiles()
{
	std::cout << "[*] Restoring configuration files..." << std::endl;
	if (g_ServerCfgFound)
	{
		if (!std::filesystem::exists(f_DedicatedConfigBackup))
		{
			std::cout << "[-] Could not find server configuration file backup!" << std::endl;
		}
		else
		{
			std::filesystem::copy_file(f_DedicatedConfigBackup, f_DedicatedConfig, std::filesystem::copy_options::overwrite_existing);
		}
		
	}
	if (g_ClientCfgFound)
	{
		if (!std::filesystem::exists(f_DedicatedConfigBackup))
		{
			std::cout << "[-] Could not find client configuration file backup!" << std::endl;
		}
		else
		{
			std::filesystem::copy_file(f_ClientConfigBackup, f_ClientConfig, std::filesystem::copy_options::overwrite_existing);
		}
		
	}
}

bool RemovePreviousInstall()
{
	
	std::cout << "[*] Download success. Performing Installation" << std::endl;
	std::cout << "[*] Checking Directories of previous verison" << std::endl;
	std::cout << "[*] Removing previous install..." << std::endl;
	if (!IsPathExist(g_NorthstarCustomDir))
	{
		std::cout << "[-] Intergrity error: Northstar.Custom does not exist" << std::endl;
	}
	else
	{
		std::filesystem::remove_all(g_NorthstarCustomDir);
	}
	if (!IsPathExist(g_NorthstarClientDir))
	{
		std::cout << "[-] Intergrity error: Northstar.Client does not exist" << std::endl;
	}
	else
	{
		std::filesystem::remove_all(g_NorthstarClientDir);
	}
	if (!IsPathExist(g_NorthstarServerDir))
	{
		std::cout << "[-] Intergrity error: Northstar.CustomServers does not exist" << std::endl;
	}
	else
	{
		std::filesystem::remove_all(g_NorthstarServerDir);
	}
	if (!IsPathExist(g_NorthstarCNCustomDir))
	{
		std::cout << "[-] Intergrity error: NorthstarCN.Custom does not exist" << std::endl;
	}
	else
	{
		std::filesystem::remove_all(g_NorthstarCNCustomDir);
	}
	
	return true;
}
void DrawErrorPrompt(int err) 
{
	MessageBoxA(
		0,
		"更新北极星CN时遇到了致命错误,北极星CN可能出现无法连接服务器、崩溃等情况。请尝试重新启动北极星CN,"
		"并将log发送至官方开黑啦以让我们获得更多信息。错误代码:" +
			err,
		"NorthstarCN自动更新",
		MB_ICONERROR | MB_OK);
}
bool ShouldDoUpdate() 
{
	//std::string remotesemver = g_RemoteVersionString.substr(1);
	//std::cout << g_LocalVersionString << std::endl;
	return g_CleanInstall || LessThanVersion(
		g_LocalVersionString, g_RemoteVersionString.erase(0, 1) + ".0"); // Remove the "v" from tagname,make up for the version numbers

}
void killProcessByName(const char* filename)
{
	HANDLE hSnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, NULL);
	PROCESSENTRY32 pEntry;
	pEntry.dwSize = sizeof(pEntry);
	BOOL hRes = Process32First(hSnapShot, &pEntry);
	while (hRes)
	{
		const char* temp;
		_bstr_t t(pEntry.szExeFile);
		temp = t;
		if (strcmp(temp, filename) == 0)
		{

			HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, 0, (DWORD)pEntry.th32ProcessID);
			if (hProcess != NULL)
			{
				TerminateProcess(hProcess, 9);
				CloseHandle(hProcess);
			}
		}
		hRes = Process32Next(hSnapShot, &pEntry);
	}
	CloseHandle(hSnapShot);
}
void TerminateNorthstarProcess() 
{
	std::cout << "[*] Terminating NorthstarCN Process...\n";
	killProcessByName("NorthstarLauncher.exe");
}




int main(int argc, char* argv[])
{

	SetConsoleOutputCP(65001);
	for (int i = 0; i < argc; i++)
		if (!strcmp(argv[i], "-force"))
			forceUpdate = true;
	
	hidecursor();

	if (forceUpdate)
	{
		std::cout << "[*] Performing update check\n";
	}
	else
	{
		std::cout << "[|] 欢迎使用北极星CN自动更新器!\n[|] 本程序将会采取必要措施以保证更新顺利完成\n[|] 比如移除所有核心Mod文件 "
					 "并强行覆盖北极星CN的所有现有文件。\n[|] 如果您对核心Mod进行了修改,您的修改内容将会被复原。 \n[|] "
					 "请注意备份保存好您的文件。 "
					 "\n[|] 按任意键开始更新。\n";
		std::cout << "[|] Welcome to NorthstarCN Updater!\n[|] This Application will aggressively update your game\n[|] by removing core mods and "
					 "overwriting the entire codebase of NorthstarCN.\n[|] Please beware that this app can damage your work \n[|] if you are doing "
					 "temporary coding on any of our core mods.\n[|] To continue update, press any key.\n";
		
		system("pause");
		
	}

	if (!GetLocalNorthstarVersion())
	{
		std::cout << "[-] Could not find NorthstarLauncher.exe!" << std::endl;
		MessageBoxA(
			0,
			"未能找到北极星CN主程序，将执行清洁安装。",
			"NorthstarCN自动更新", MB_ICONINFORMATION | MB_OK);
		g_CleanInstall = true;

	}

	if (GetLatestNorthstarVersion())
	{
		if (!ShouldDoUpdate())
		{
			std::cout << "[*] NorthstarCN is on latest version!" << std::endl;
			if (!forceUpdate)
			{
				std::cout << "[*] ";
				// Let people press some key to exit so its not like crashing when they are on latest version
				system("pause");
			}
			return 0;
		}
		// Actual install procedure starts here
		
		TerminateNorthstarProcess();

		if (!CheckAndGetLatestRelease())
		{
			// Download failed
			std::cout << "[-] Download Failed! NorthstarCN may not function properly. prease consider retrying!" << std::endl;
			DrawErrorPrompt(1);
			return 1;
		}
		
		// Download success
		
		// Backup here before we clean the mods
		
		BackupConfigFiles(); 
		

		
		if (!RemovePreviousInstall())
		{
			std::cout << "[-] Error occurred while removing previous Install!" << std::endl;
			DrawErrorPrompt(2);
			return 2;
			
		
		}
		
		if (!CheckUnzipEnvironment())
		{
			std::cout << "[-] Error occurred while checking unzip environment!" << std::endl;
			MessageBoxA(
				0,
				"错误:未能找到unzip.exe,并在尝试获取时发生了致命错误。请确认zip包内文件解压是否完整,并检查您的网络连接。",
				"NorthstarCN自动更新",
				MB_ICONERROR | MB_OK);
			return 3;
			
		}
		if (!VerifyPackageIntegrity())
		{
			std::cout << "[-] Error occurred while removing previous Install!" << std::endl;
			DrawErrorPrompt(6);
			return 6;
		
		}

		if (!UnpackLatestToTemp())
		{
			std::cout << "[-] Error occurred while unpacking file!" << std::endl;
			DrawErrorPrompt(4);
			return 4;
			
		}

		
		RestoreConfigFiles();

		CleanTempFiles();
	}
	else
	{
		std::cout << "[-] Error occurred while reading latest NorthstarCN version!" << std::endl;
		DrawErrorPrompt(5);
		return 5;
	
	}
	
	if (forceUpdate)
	{
		std::cout << "[*] Restarting NorthstarCN...\n";
		STARTUPINFO info = {sizeof(info)};
		PROCESS_INFORMATION processInfo;
		if (CreateProcess(L"NorthstarLauncher.exe", LPWSTR(""), NULL, NULL, TRUE, 0, NULL, NULL, &info, &processInfo))
		{
			CloseHandle(processInfo.hProcess);
			CloseHandle(processInfo.hThread);
		}
	}
	std::cout << "[*] All done! Have fun Pilot! :)\n";
	std::cout << "[*] Exiting in 5 seconds.\n";

	Sleep(5000);

	return 0;



	
	//system("pause");
}


