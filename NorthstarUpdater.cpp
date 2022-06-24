// NorthstarUpdater.cpp : This file contains the 'main' function. Program execution begins and ends there.

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
#pragma comment(lib, "include/libcurl/lib/libcurl_a.lib")
#pragma comment(lib, "version.lib")

#pragma comment(lib, "Normaliz.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Wldap32.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "advapi32.lib")
std::string g_NorthstarCustomDir = "R2Northstar\\mods\\Northstar.Custom";
std::string g_NorthstarServerDir = "R2Northstar\\mods\\Northstar.CustomServers";
std::string g_NorthstarClientDir = "R2Northstar\\mods\\Northstar.Client";
std::string g_NorthstarCNCustomDir = "R2Northstar\\mods\\NorthstarCN.Custom";

std::string g_TempFolderName = "UpdaterTemp";
std::string g_TempPackageName = "Latest.zip";
int g_VersionFetchTimeoutLimit = 50; // Timeout Limit fetching latest version, *100msec
std::string g_MasterServerAddress = "nscn.wolf109909.top";
std::string g_MasterServerVersionEndPoint = "https://nscn.wolf109909.top/version/query";
bool g_ShouldPerformUpdate = false;
bool forceUpdate = false;
bool g_SuccessfullyFetchedLatestVersion = false;
std::string g_RemoteVersionString;
std::string g_LocalVersionString;
std::string g_LatestVersionURL;
#include <math.h>

int progress_func(void* ptr, double TotalToDownload, double NowDownloaded, double TotalToUpload, double NowUploaded)
{
	// ensure that the file to be downloaded is not empty
	// because that would cause a division by zero error later on
	if (TotalToDownload <= 0.0)
		{
			return 0;
		}

	// how wide you want the progress meter to be
	int totaldotz = 40;
	double fractiondownloaded = NowDownloaded / TotalToDownload;
	// part of the progressmeter that's already "full"
	int dotz = (int)round(fractiondownloaded * totaldotz);

	// create the "meter"
	int ii = 0;
	printf("%3.0f%% [", fractiondownloaded * 100);
	// part  that's full already
	for (; ii < dotz; ii++)
	{
		printf("=");
	}
	// remaining part (spaces)
	for (; ii < totaldotz; ii++)
	{
		printf(" ");
	}
	// and back to line begin - do not forget the fflush to avoid output buffering problems!
	printf("]\r");
	fflush(stdout);
	// if you don't return 0, the transfer will be aborted - see the documentation
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
void GetLatestNorthstarVersion()
{

	std::thread requestThread(
		[]()
		{
			std::string urljsonstr;
			std::cout << "[*] Fetching Latest NorthstarCN Version" << std::endl;

			CURL* curl = curl_easy_init();
			SetCommonHttpClientOptions(curl);
			std::string readBuffer;
			char* errBuffer;
			curl_easy_setopt(curl, CURLOPT_URL, g_MasterServerVersionEndPoint.c_str());
			curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, &errBuffer);
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

				//g_LatestVersionURL = reply["assets"][0];
				std::stringstream ss;
				for (auto& x : reply["assets"].items())
				{
					ss << x.value();
				}
				auto assetobj = nlohmann::json::parse(ss.str());
				g_LatestVersionURL = assetobj["browser_download_url"];

				std::cout << "[*] Found latest version: " << g_RemoteVersionString << std::endl;
				//semver::version v1 {g_RemoteVersionString};
				g_SuccessfullyFetchedLatestVersion = true;
			}
			else
			{
				std::cout << errBuffer << std::endl;
			}

			// we goto this instead of returning so we always hit this
		REQUEST_END_CLEANUP:

			curl_easy_cleanup(curl);
		});

	requestThread.detach();
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
		std::cout << std::filesystem::current_path() << std::endl;
		fp = fopen((g_TempFolderName + "\\" + g_TempPackageName).c_str(), "wb");
		curl_easy_setopt(curl, CURLOPT_URL, g_LatestVersionURL.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
		/* we tell libcurl to follow redirection */
		// Internal CURL progressmeter must be disabled if we provide our own callback
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, FALSE);
		// Install the callback function
		curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_func); 
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

		//curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		std::cout << "[*] Downloading... " << g_RemoteVersionString << std::endl;
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
		return true;
	}
	return false;
}

bool CheckAndGetLatestRelease() 
{
	if (!IsPathExist(g_TempFolderName))
	{
		std::cout << "Can't find temp folder. Creating a new one" << std::endl;
		std::filesystem::create_directory(g_TempFolderName);
		

	}

	std::cout << "Staring Download:" << g_LatestVersionURL << std::endl;
	//downloadUrl((g_TempFolderName + "/" + g_TempPackageName).c_str(), g_LatestVersionURL.c_str());
	if (!downloadLatestRelease())
	{
		return false;
	}
	
	return true;
}


bool VersionFetchSuccess()
{
	int VersionFetchTimeout = 0;
	while (!g_SuccessfullyFetchedLatestVersion || VersionFetchTimeout > g_VersionFetchTimeoutLimit)
	{
		Sleep(100);
		VersionFetchTimeout++;
	}
	if (g_SuccessfullyFetchedLatestVersion)
	{
		return true;
	}
	else
	{
		return false;
	}
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
	std::string command = "unzip -q -o " + sourcefile + " -x NorthstarUpdater.exe unzip.exe";
	system(command.c_str());
	

	

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
	
	return LessThanVersion(
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
	for (int i = 0; i < argc; i++)
		if (!strcmp(argv[i], "-force"))
			forceUpdate = true;

	if (forceUpdate)
	{
		std::cout << "[*] Performing update check\n";
	}
	else
	{

		std::cout << "Welcome to NorthstarCN Updater!\nThis Application will aggressively update your game\nby removing core mods and "
					 "overwriting the entire codebase of NorthstarCN.\nPlease beware that this app can damage your work \nif you are doing "
					 "temporary coding on any of our core mods.\nTo continue update, press any key.\n";
		system("pause");
	}

	if (!GetLocalNorthstarVersion())
	{
		std::cout << "[-] Could not find NorthstarLauncher.exe!" << std::endl;
		MessageBoxA(
			0,
			"未能找到北极星CN主程序，将执行清洁安装。",
			"NorthstarCN自动更新", MB_ICONINFORMATION | MB_OK);
		
	}

	GetLatestNorthstarVersion();

	
	
	


	if (VersionFetchSuccess())
	{
		if (!ShouldDoUpdate())
		{
			std::cout << "[*] NorthstarCN is on latest version!" << std::endl;
			if (!forceUpdate)
			{
				system("pause");
			}
			return 0;
		}
		//Actual install procedure starts here

		TerminateNorthstarProcess();

		if (!CheckAndGetLatestRelease())
		{
			//Download failed
			std::cout << "[-] Download Failed! NorthstarCN may not function properly. prease consider retrying!" << std::endl;
			DrawErrorPrompt(1);
			return 1;
		}
		
		//Download success
		if (!RemovePreviousInstall())
		{
			std::cout << "[-] Error occurred while remove previous Install!" << std::endl;
			return 2;
			DrawErrorPrompt(2);
		
		}
		if (!UnpackLatestToTemp())
		{
			std::cout << "[-] Error occurred while unpacking file!" << std::endl;
			return 3;
			DrawErrorPrompt(3);
		}
		CleanTempFiles();
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

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
