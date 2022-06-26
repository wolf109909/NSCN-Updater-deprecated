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
#include "include/color.hpp"
#include <codecvt>
#include <indicators/cursor_control.hpp>
#include <indicators/progress_bar.hpp>
#include <indicators/block_progress_bar.hpp>
#include <indicators/indeterminate_progress_bar.hpp>
#include <indicators/progress_spinner.hpp>
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
bool isRetryingDownload = false;
std::string g_RemoteVersionString;
std::string g_LocalVersionString;
std::string g_LatestVersionURL;
int g_NorthstarPackageDownloadSize;
int lastconsolecolor = 0;//white
using namespace indicators;

int nb_bar;
double last_progress, progress_bar_adv;

HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

int legacy_progress_bar(void* ptr, double TotalToDownload, double NowDownloaded, double TotalToUpload, double NowUploaded)
{
	// ensure that the file to be downloaded is not empty
	// because that would cause a division by zero error later on
	if (TotalToDownload <= 0.0)
	{
		return 0;
	}

	SetConsoleTextAttribute(hConsole, 10);
	
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
	SetConsoleTextAttribute(hConsole, 15);
	fflush(stdout);
	// if you don't return 0, the transfer will be aborted - see the documentation
	
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
	std::cout << dye::aqua("[*] 正在获取本地北极星CN版本...\n");
	
	GetFileVersion("NorthstarLauncher.exe", version);
	std::cout << dye::green("[*] 检测到本地北极星CN版本：") << dye::green(version) << std::endl;
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
		std::cout << dye::aqua("[*] 正在下载 GNU unzip... \n") << std::endl;
		if (forceUpdate)
		{
			curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, legacy_progress_bar);
			
		}
		else
		{
			BlockProgressBar bar {
				option::BarWidth {60},
				option::Start {"["},
				option::End {"]"},
				option::ForegroundColor {Color::yellow},
				option::FontStyles {std::vector<FontStyle> {FontStyle::bold}}};

			curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, &bar);
			curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_func);
		}
		
		res = curl_easy_perform(curl);
		/* always cleanup */
		if (res != CURLE_OK)
		{
			fprintf(stderr, "[-] CURL无法完成请求: %s\n", curl_easy_strerror(res));
			curl_easy_cleanup(curl);
			fclose(fp);
			return false;
		}
		curl_easy_cleanup(curl);
		fclose(fp);
		SetConsoleTextAttribute(hConsole, 15);
		std::cout << dye::green("[*] 下载成功!\n") << std::endl;
		return true;
		std::cout << std::endl;//hey wolf are u here? bad bad
	}
	return false;


}



bool GetUnzipApplication() 
{
	std::cout << dye::aqua("[*] 正在检查GNU Unzip...\n");
	
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
		std::cout << dye::yellow("[*] 警告：无法在根目录下找到GNU Unzip,正在尝试获取...\n");
		

		if (!TryDownloadUnzip())
		{
			std::cout << dye::red("[-] 错误：无法下载 GNU Unzip,下载失败!\n");
			return false;
		}
	}
	return true;
	std::cout << dye::green("[*] 成功获取GNU Unzip!\n");
}
bool GetLatestNorthstarVersion()
{

		std::string urljsonstr;
		std::cout << dye::aqua("[*] 正在获取北极星CN最新版本...") << std::endl;

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

			std::cout << dye::green("[*] 成功获取最新北极星CN版本: ") << g_RemoteVersionString << std::endl;
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
	
	std::cout << dye::aqua("[*] 正在检查安装包文件完整性...\n");
	std::cout << dye::aqua("[*] 文件大小: ") << g_NorthstarPackageDownloadSize << " / " << downloadedsize << std::endl;
	if (g_NorthstarPackageDownloadSize != downloadedsize)
	{
		std::cout << dye::red("[-] 错误：文件大小不匹配!\n");
		return false;
	}
	if (downloadedsize < 100000)
	{
		std::cout << dye::red("[-] 错误：文件损坏!\n");
		MessageBoxA(
			0,
			"更新北极星CN时服务器返回了错误的文件,请尝试重新执行更新操作。错误代码: 7" ,"NorthstarCN自动更新",
			MB_ICONERROR | MB_OK);
		return false;
	}

	std::cout << dye::green("[*] 文件完整性检查通过!\n");
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
		std::cout << dye::aqua("[*] 正在下载 : ") << g_RemoteVersionString << std::endl;
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
				option::BarWidth {60},
				option::Start {"["},
				option::End {"]"},
				option::ForegroundColor {Color::yellow},
				option::FontStyles {std::vector<FontStyle> {FontStyle::bold}}};

			curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, &bar);
			curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_func); 
		}
		

		
		

		
		res = curl_easy_perform(curl);  
		curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &g_NorthstarPackageDownloadSize);
		//std::cout << g_NorthstarPackageDownloadSize << std::endl;
		if (g_NorthstarPackageDownloadSize < 10000)
		{
			if (isRetryingDownload)
			{
				return false;
			}
			else
			{
				isRetryingDownload = true;
			}
			std::cout << dye::red("\n[-] 错误:服务器返回内容为空!\n");
			std::cout << dye::aqua("[*] 正在准备重试...\n");
			int RetryLimit = 3;
			int RetryCount = 0;
			bool RetrySuccess = false;
			while (RetryCount<RetryLimit)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(3000));
				
				std::cout << dye::aqua("[-] 正在尝试下载: ") << (RetryCount+1) << "/" << RetryLimit << std::endl;
				
				RetrySuccess = downloadLatestRelease();
				
				if (RetrySuccess)
				{
					break;
				}
				
				RetryCount++;
				std::cout << dye::red("[-] 下载失败!\n");
				std::cout << dye::aqua("[*] 将在3秒后再试...\n");
				
			}
			

			if (RetrySuccess)
			{
				SetConsoleTextAttribute(hConsole, 15);
				std::cout << dye::green("[*] 下载成功!\n");
				return true;
			}
			else
			{
				std::cout << dye::red("[-] 错误：无法完成下载,服务器可能出错!请在北极星CN官方开黑啦中通知我们!\n");
				return false;
			}
			curl_easy_cleanup(curl);
			return false;
		}
		/* always cleanup */
		if (res != CURLE_OK)
		{
			fprintf(stderr, "[-] CURL无法完成请求: %s\n", curl_easy_strerror(res));
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
		std::cout << dye::aqua("[*] 无法定位已知临时缓存目录,正在新建目录...") << std::endl;
		std::filesystem::create_directory(g_TempFolderName);
		

	}

	std::cout << dye::green("[*] 成功获取URL:") << g_LatestVersionURL << std::endl;
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
	std::cout << dye::aqua("[*] 正在安装北极星CN...") << std::endl;
	// zip exists, so just drop it in temp folder.

	if (forceUpdate)
	{
		std::string command = "unzip -q -o " + sourcefile + " -x NorthstarUpdater.exe unzip.exe";
		system(command.c_str());
	
	}
	else
	{
		indicators::ProgressSpinner spinner {
			option::PostfixText {" 正在解压缩..."},
			option::ForegroundColor {Color::yellow},
			option::ShowPercentage {false},
			option::SpinnerStates {std::vector<std::string> {"[⠈]", "[⠐]", "[⠠]", "[⢀]", "[⡀]", "[⠄]", "[⠂]", "[⠁]"}},
			option::FontStyles {std::vector<FontStyle> {FontStyle::bold}}};

		auto job = [&spinner, sourcefile]
		{
			std::string command = "unzip 1>NUL 2>NUL -qq -o " + sourcefile + " -x NorthstarUpdater.exe unzip.exe";
			system(command.c_str());
			spinner.mark_as_completed();
		};

		std::thread unzip_thread(job);

		while (true)
		{
			if (spinner.is_completed())
			{
				spinner.set_option(option::ForegroundColor {Color::green});
				spinner.set_option(option::PrefixText {"[*]"});
				spinner.set_option(option::ShowSpinner {false});
				spinner.set_option(option::ShowPercentage {false});
				spinner.set_option(option::PostfixText {" 解压缩完成!"});
				spinner.mark_as_completed();
				break;
			}
			else
				spinner.tick();
			std::this_thread::sleep_for(std::chrono::milliseconds(40));
		}

		unzip_thread.join();
	}
	

}
void CleanTempFiles() 
{
	std::cout << dye::green("[*] 北极星CN安装完成!") << std::endl;
	std::cout << dye::aqua("[*] 正在移除临时文件...") << std::endl;
	if (!IsPathExist(g_TempFolderName))
	{
		std::cout << dye::red("[-] 错误：未发现临时文件,程序执行可能出现了未知的严重问题!") << std::endl;
	}
	else
	{
		std::filesystem::remove_all(g_TempFolderName);
	}
}
void BackupConfigFiles() 
{
	std::cout << dye::aqua("[*] 正在备份配置文件...") << std::endl;
	if (!std::filesystem::exists(f_DedicatedConfig))
	{
		std::cout << dye::yellow("[*] 警告：无法找到旧版服务端配置文件!") << std::endl;
		g_ServerCfgFound = false;
	}
	if (!std::filesystem::exists(f_ClientConfig))
	{
		std::cout << dye::yellow("[*] 警告：无法找到旧版客户端配置文件!") << std::endl;
		g_ClientCfgFound = false;
	}
	if (!g_ServerCfgFound && !g_ClientCfgFound)
	{
		std::cout << "[*] 备份失败!未能查找到旧版配置文件!" << std::endl;
		
		return;
	}

	if (g_ServerCfgFound)
	{
		std::cout << dye::aqua("[*] 正在备份服务端配置文件...") << std::endl;
		std::filesystem::copy_file(f_DedicatedConfig, f_DedicatedConfigBackup, std::filesystem::copy_options::overwrite_existing);
	}
	if (g_ClientCfgFound)
	{
		std::cout << dye::aqua("[*] 正在备份客户端配置文件...") << std::endl;
		std::filesystem::copy_file(f_ClientConfig, f_ClientConfigBackup, std::filesystem::copy_options::overwrite_existing);
	}
	
}
void RestoreConfigFiles()
{
	std::cout << dye::aqua("[*] 正在恢复配置文件备份...") << std::endl;
	if (g_ServerCfgFound)
	{
		if (!std::filesystem::exists(f_DedicatedConfigBackup))
		{
			std::cout << dye::yellow("[-] 警告：无法找到旧版客户端配置文件备份,您的设置将会被重置!") << std::endl;
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
			std::cout << dye::yellow("[-] 警告：无法找到旧版服务端配置文件备份,您的设置将会被重置!") << std::endl;
		}
		else
		{
			std::filesystem::copy_file(f_ClientConfigBackup, f_ClientConfig, std::filesystem::copy_options::overwrite_existing);
		}
		
	}
}

bool RemovePreviousInstall()
{
	
	//std::cout << dye::green("[*] 下载成功,即将开始安装!") << std::endl;
	std::cout << dye::aqua("[*] 正在检查旧版文件...") << std::endl;
	
	if (!IsPathExist(g_NorthstarCustomDir))
	{
		std::cout << dye::yellow("[-] 文件检查： Northstar.Custom 不存在") << std::endl;
	}
	else
	{
		std::cout << dye::aqua("[*] 正在移除旧版 Northstar.Custom ...") << std::endl;
		std::filesystem::remove_all(g_NorthstarCustomDir);
	}
	if (!IsPathExist(g_NorthstarClientDir))
	{
		std::cout << dye::yellow("[-] 文件检查： Northstar.Client 不存在") << std::endl;
	}
	else
	{
		std::cout << dye::aqua("[*] 正在移除旧版 Northstar.Client ...") << std::endl;
		std::filesystem::remove_all(g_NorthstarClientDir);
	}
	if (!IsPathExist(g_NorthstarServerDir))
	{
		std::cout << dye::yellow("[-] 文件检查：Northstar.CustomServers 不存在") << std::endl;
	}
	else
	{
		std::cout << dye::aqua("[*] 正在移除旧版 Northstar.CustomServers ...") << std::endl;
		std::filesystem::remove_all(g_NorthstarServerDir);
	}
	if (!IsPathExist(g_NorthstarCNCustomDir))
	{
		std::cout << dye::yellow("[-] 文件检查： NorthstarCN.Custom 不存在") << std::endl;
	}
	else
	{
		std::cout << dye::aqua("[*] 正在移除旧版 NorthstarCN.Custom ...") << std::endl;
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
	std::cout << dye::aqua("[*] 正在关闭北极星CN进程...\n");
	killProcessByName("NorthstarLauncher.exe");
}


bool GetTitanfall2Main() 
{
	std::cout << dye::aqua("[*] 正在检查当前目录是否为游戏根目录...\n");
	if (!std::filesystem::exists("Titanfall2.exe"))
	{
		std::cout << dye::red("[-] 错误：无法在当前目录下找到《泰坦陨落2》主程序\n");
		return false;
	}
	else
	{
		std::cout << dye::green("[*] 成功找到《泰坦陨落2》主程序\n");
		return true;
	}
	
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
		std::cout << "[*] 欢迎使用北极星CN自动更新器!\n[*] 本程序将会采取必要措施以保证更新顺利完成\n[*] 比如移除所有核心Mod文件 "
					 "并强行覆盖北极星CN的所有现有文件。\n[*] 如果您对核心Mod进行了修改,您的修改内容将会被复原。 \n[*] "
					 "请注意备份保存好您的文件。 "<<
					 dye::on_white("\n[*] 按任意键开始更新\n");
		//std::cout << "[|] Welcome to NorthstarCN Updater!\n[|] This Application will aggressively update your game\n[|] by removing core mods and "
					// "overwriting the entire codebase of NorthstarCN.\n[|] Please beware that this app can damage your work \n[|] if you are doing "
					// "temporary coding on any of our core mods.\n[|] To continue update, press any key.\n";
		
		//std::cout << "[*] 按任意键开始更新\n";
		//std::cout << "[*] Press any key to begin.\n";
		system("pause 1>NUL 2>NUL");
		
	}

	if (!GetTitanfall2Main())
	{
		MessageBoxA(0, "未能找到《泰坦陨落2》主程序，安装程序无法继续运行。请检查当前目录是否为游戏根目录!", "NorthstarCN自动更新", MB_ICONERROR | MB_OK);
		return 10;
	}
	

	if (!GetLocalNorthstarVersion())
	{
		std::cout << dye::yellow("[-] 警告：无法找到北极星CN主程序 - NorthstarLauncher.exe !") << std::endl;
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
			//std::cout << "[*] NorthstarCN is on latest version!" << std::endl;
			if (!forceUpdate)
			{
				std::cout << dye::on_white("[*] 北极星CN已经是最新版本,请按任意键结束\n");
				//std::cout << "[*] Press any key to exit.\n";
				system("pause 1>NUL 2>NUL");
			}
			return 0;
		}
		// Actual install procedure starts here
		
		TerminateNorthstarProcess();

		if (!CheckAndGetLatestRelease())
		{
			// Download failed
			std::cout << dye::red("[-] 错误：下载北极星CN最新版本安装包时出现错误,请检查互联网连接并稍后再试") << std::endl;
			DrawErrorPrompt(1);
			return 1;
		}
		
		// Download success
		
		// Backup here before we clean the mods
		
		BackupConfigFiles(); 
		

		
		if (!RemovePreviousInstall())
		{
			std::cout << dye::red("[-] 移除旧版北极星CN文件时出现错误,请检查文件是否有操作权限") << std::endl;
			DrawErrorPrompt(2);
			return 2;
			
		
		}
		
		if (!CheckUnzipEnvironment())
		{
			std::cout << dye::red("[-] 错误：无法验证解压环境是否正常!") << std::endl;
			MessageBoxA(
				0,
				"错误:未能找到unzip.exe,并在尝试获取时发生了致命错误。请确认zip包内文件解压是否完整,并检查您的网络连接。",
				"NorthstarCN自动更新",
				MB_ICONERROR | MB_OK);
			return 3;
			
		}
		if (!VerifyPackageIntegrity())
		{
			std::cout << dye::red("[-] 错误：压缩包实际大小与服务器文件大小不一致,请检查互联网连接并稍后再试") << std::endl;
			DrawErrorPrompt(6);
			return 6;
		
		}

		if (!UnpackLatestToTemp())
		{
			std::cout << dye::red("[-] 错误：无法解压安装包,请检查互联网连接并稍后再试") << std::endl;
			DrawErrorPrompt(4);
			return 4;
			
		}

		
		RestoreConfigFiles();

		CleanTempFiles();
	}
	else
	{
		std::cout << dye::red("[-] 错误：无法获取北极星CN最新版本,请检查互联网连接") << std::endl;
		DrawErrorPrompt(5);
		return 5;
	
	}
	
	if (forceUpdate)
	{
		std::cout << dye::aqua("[*] 正在重新启动北极星CN...\n");
		STARTUPINFO info = {sizeof(info)};
		PROCESS_INFORMATION processInfo;
		if (CreateProcess(L"NorthstarLauncher.exe", LPWSTR(""), NULL, NULL, TRUE, 0, NULL, NULL, &info, &processInfo))
		{
			CloseHandle(processInfo.hProcess);
			CloseHandle(processInfo.hThread);
		}
	}
	std::cout << dye::green("[*] 更新作业成功完成,祝您游戏愉快,铁驭! \n");
	//std::cout << "[*] All done! Have fun Pilot! :)\n";
	
	if (forceUpdate)
	{
		std::cout << "[*] 将会在5秒后退出\n";
		Sleep(5000);
	}
	else
	{
		std::cout << dye::on_white("[*] 按任意键结束\n");
		//std::cout << "[*] Press any key to exit.\n";
		system("pause 1>NUL 2>NUL");
	}
	

	return 0;



	
	//system("pause");
}


