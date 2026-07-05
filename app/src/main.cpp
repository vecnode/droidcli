#include "metaagent_host.hpp"
#include "metaagent_http_mount.hpp"

#ifdef __GNUC__
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wshadow"
# pragma GCC diagnostic ignored "-Wconversion"
#endif

#include <httplib.h>
#include <cpp-embedlib-httplib.h>
#include "MetaAgentWebAssets.h"
#include "webview/webview.h"

#ifdef __GNUC__
# pragma GCC diagnostic pop
#endif

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/post.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __linux__
#include <gtk/gtk.h>
#include <unistd.h>
#endif

#ifndef METAAGENT_APP_INCLUDE_TERMINAL_ON_RELEASE
#define METAAGENT_APP_INCLUDE_TERMINAL_ON_RELEASE 0
#endif

namespace {

constexpr std::size_t kMaxRequestBodyBytes = 4 * 1024 * 1024;
// App title/version follow the library version (0.2.0 — hold here, no 0.3 yet).
constexpr const char* kAppVersion = METAAGENT_VERSION_STRING;
constexpr const char* kAppTitle = "agent core " METAAGENT_VERSION_STRING;
constexpr int kWindowWidth = 1920;
constexpr int kWindowHeight = 1080;

std::string env_or_default(const char* name, const char* default_value)
{
#if defined(_WIN32)
	char* buffer = nullptr;
	size_t length = 0;
	if (_dupenv_s(&buffer, &length, name) == 0 && buffer != nullptr)
	{
		const std::string value = buffer;
		free(buffer);
		if (!value.empty())
		{
			return value;
		}
	}
#else
	if (const char* value = std::getenv(name))
	{
		if (value[0] != '\0')
		{
			return value;
		}
	}
#endif
	return default_value;
}

bool env_flag_enabled(const char* name)
{
	const std::string value = env_or_default(name, "0");
	return value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "YES";
}

// Same truthiness check, but the flag is ON unless explicitly disabled.
bool env_flag_enabled_default_on(const char* name)
{
	const std::string value = env_or_default(name, "1");
	return !(value == "0" || value == "false" || value == "FALSE" || value == "no" || value == "NO");
}

int env_int_or_default(const char* name, const int default_value)
{
	const std::string value = env_or_default(name, "");
	if (value.empty())
	{
		return default_value;
	}
	try
	{
		return std::stoi(value);
	}
	catch (const std::exception&)
	{
		return default_value;
	}
}

metaagent::app_host::HostConfig load_host_config()
{
	metaagent::app_host::HostConfig config;
	config.enable_ai = !env_flag_enabled("METAAGENT_NO_AI");
	config.ollama_url = env_or_default("METAAGENT_OLLAMA_URL", "http://127.0.0.1:11434");
	config.ollama_model = env_or_default("METAAGENT_OLLAMA_MODEL", "llama3.2");
	config.system_prompt = env_or_default(
		"METAAGENT_SYSTEM_PROMPT",
		"You are a concise assistant embedded in the MetaAgent desktop application.");
	config.media_player_base_url = env_or_default(
		"METAAGENT_MEDIA_PLAYER_URL",
		"http://127.0.0.1:8080");
	config.media_data_directory = env_or_default("METAAGENT_MEDIA_DATA_DIR", "");
	config.adapter_url = env_or_default("METAAGENT_ADAPTER_URL", "http://127.0.0.1:8008");
	config.media_player_project_dir = env_or_default("METAAGENT_MEDIA_PLAYER_DIR", "");
	config.media_player_build_command = env_or_default("METAAGENT_MEDIA_BUILD_CMD", "make Release");
	config.media_player_run_command = env_or_default("METAAGENT_MEDIA_RUN_CMD", "media-player-cpp.exe");
	config.adapter_project_dir = env_or_default("METAAGENT_ADAPTER_DIR", "");
	config.adapter_launch_command = env_or_default("METAAGENT_ADAPTER_LAUNCH_CMD", "deploy.bat");
	config.dataset_output_dir = env_or_default("METAAGENT_DATASET_DIR", "");
	config.auto_start_media_player = env_flag_enabled_default_on("METAAGENT_AUTOSTART_MEDIA_PLAYER");
	config.auto_start_adapter = env_flag_enabled_default_on("METAAGENT_AUTOSTART_ADAPTER");
	config.google_api_key = env_or_default("METAAGENT_GOOGLE_API_KEY", "");
	config.google_search_engine_id = env_or_default("METAAGENT_GOOGLE_CSE_ID", "");
	config.google_search_query = env_or_default("METAAGENT_GOOGLE_SEARCH_QUERY", "");
	config.google_search_interval_seconds =
		env_int_or_default("METAAGENT_GOOGLE_SEARCH_INTERVAL_SECONDS", 10);
	return config;
}

std::filesystem::path executable_directory()
{
#if defined(_WIN32)
	char buffer[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
	{
		return {};
	}
	return std::filesystem::path(buffer).parent_path();
#elif defined(__linux__)
	std::error_code ec;
	const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
	return ec ? std::filesystem::path {} : self.parent_path();
#else
	return {};
#endif
}

// Distribution auto-discovery: when metaagent runs from the dist layout
// (metaagent/ next to media-player/, adapter/, datasets/ — see
// build_and_distribute.bat), default any unset paths to those siblings so an
// unzipped dist works with zero configuration. Env vars always win.
void apply_dist_layout_defaults(metaagent::app_host::HostConfig& config)
{
	const std::filesystem::path exe_dir = executable_directory();
	if (exe_dir.empty())
	{
		return;
	}
	const std::filesystem::path root = exe_dir.parent_path();
	std::error_code ec;

	const auto exists_dir = [&ec](const std::filesystem::path& path)
	{
		return std::filesystem::is_directory(path, ec);
	};

	if (config.media_player_project_dir.empty() && exists_dir(root / "media-player"))
	{
		config.media_player_project_dir = (root / "media-player").string();
	}
	if (config.adapter_project_dir.empty() && exists_dir(root / "adapter" / "deploy"))
	{
		config.adapter_project_dir = (root / "adapter" / "deploy").string();
	}
	if (config.dataset_output_dir.empty() && exists_dir(root / "datasets"))
	{
		config.dataset_output_dir = (root / "datasets").string();
	}
	if (config.media_data_directory.empty() && exists_dir(root / "media-player" / "data"))
	{
		config.media_data_directory = (root / "media-player" / "data").string();
	}
}

void schedule_tick(
	boost::asio::steady_timer& timer,
	metaagent::app_host::MetaAgentHost& host,
	std::chrono::milliseconds interval)
{
	timer.expires_after(interval);
	timer.async_wait([&timer, &host, interval](const boost::system::error_code& error)
	{
		if (error)
		{
			return;
		}
		host.tick(static_cast<float>(interval.count()) / 1000.0f);
		schedule_tick(timer, host, interval);
	});
}

#ifdef _WIN32
std::filesystem::path executable_dir()
{
	wchar_t path[MAX_PATH] {};
	const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
	{
		return std::filesystem::current_path();
	}
	return std::filesystem::path(path).parent_path();
}

void apply_windows_icons(webview::webview& window)
{
	const auto window_result = window.window();
	if (!window_result.ok())
	{
		return;
	}

	const auto hwnd = static_cast<HWND>(window_result.value());
	if (!hwnd)
	{
		return;
	}

	const auto assets_dir = executable_dir() / "assets";
	const auto small_icon_path = (assets_dir / "icon_topbar_small.ico").wstring();
	const auto large_icon_path = (assets_dir / "icon_window_bar.ico").wstring();

	const auto small_icon = static_cast<HICON>(LoadImageW(
		nullptr,
		small_icon_path.c_str(),
		IMAGE_ICON,
		GetSystemMetrics(SM_CXSMICON),
		GetSystemMetrics(SM_CYSMICON),
		LR_LOADFROMFILE));

	const auto large_icon = static_cast<HICON>(LoadImageW(
		nullptr,
		large_icon_path.c_str(),
		IMAGE_ICON,
		GetSystemMetrics(SM_CXICON),
		GetSystemMetrics(SM_CYICON),
		LR_LOADFROMFILE));

	if (small_icon)
	{
		SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
	}
	if (large_icon)
	{
		SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large_icon));
	}

	if (IsIconic(hwnd))
	{
		ShowWindow(hwnd, SW_RESTORE);
	}

	const DWORD style = GetWindowLongW(hwnd, GWL_STYLE);
	SetWindowLongW(
		hwnd,
		GWL_STYLE,
		style & ~(WS_MAXIMIZEBOX | WS_THICKFRAME | WS_SIZEBOX));
	SetWindowPos(
		hwnd,
		nullptr,
		0,
		0,
		kWindowWidth,
		kWindowHeight,
		SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}
#endif

#ifdef __linux__
std::filesystem::path executable_dir()
{
	char path[4096] {};
	const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
	if (length <= 0)
	{
		return std::filesystem::current_path();
	}
	path[length] = '\0';
	return std::filesystem::path(path).parent_path();
}

void apply_linux_icon(webview::webview& window)
{
	const auto window_result = window.window();
	if (!window_result.ok())
	{
		return;
	}

	auto* const gtk_window = GTK_WINDOW(window_result.value());
	if (!gtk_window)
	{
		return;
	}

	const std::filesystem::path candidates[] = {
		executable_dir() / "assets" / "icon_window_bar.png",
		executable_dir() / "assets" / "icon_topbar_small.png",
	};

	for (const auto& icon_path : candidates)
	{
		if (!std::filesystem::exists(icon_path))
		{
			continue;
		}

		GError* error = nullptr;
		if (gtk_window_set_icon_from_file(gtk_window, icon_path.string().c_str(), &error))
		{
			if (error)
			{
				g_error_free(error);
			}
			return;
		}

		if (error)
		{
			g_error_free(error);
		}
	}
}
#endif

} // namespace

int main()
{
#ifdef _WIN32
	// The console's default OEM codepage isn't UTF-8, so raw UTF-8 bytes
	// (e.g. curly quotes/em-dashes in Ollama-sourced subtitle text) printed
	// via std::cout/std::cerr render as mojibake without this.
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
#endif

	std::cout << "metaagent-app v" << kAppVersion << std::endl;

	metaagent::app_host::MetaAgentHost host;
	metaagent::app_host::HostConfig host_config = load_host_config();
	apply_dist_layout_defaults(host_config);
	host.configure(host_config);
	host.initialize();

	boost::asio::io_context io_context;
	auto work_guard = boost::asio::make_work_guard(io_context);

	boost::asio::steady_timer tick_timer {io_context};
	schedule_tick(tick_timer, host, std::chrono::milliseconds {16});

	std::thread io_thread([&io_context]() { io_context.run(); });

	httplib::Server server;
	server.set_read_timeout(std::chrono::seconds {3});
	server.set_write_timeout(std::chrono::seconds {3});
	server.set_keep_alive_max_count(1);
	server.set_payload_max_length(kMaxRequestBodyBytes);

	metaagent::app_host::mount_metaagent_routes(server, host);
	httplib::mount(server, MetaAgentWeb::FS);

	const int port = server.bind_to_any_port("127.0.0.1");
	if (port <= 0)
	{
		std::cerr << "[fatal] Failed to bind HTTP server to 127.0.0.1" << std::endl;
		return 1;
	}

	std::cout << "metaagent-app listening on http://127.0.0.1:" << port << std::endl;

	std::thread server_thread([&server]() { server.listen_after_bind(); });

	webview::webview window(false, nullptr);
	window.set_title(kAppTitle);
	window.set_size(kWindowWidth, kWindowHeight, WEBVIEW_HINT_FIXED);
#ifdef _WIN32
	apply_windows_icons(window);
#endif
#ifdef __linux__
	apply_linux_icon(window);
#endif
	window.navigate("http://127.0.0.1:" + std::to_string(port));
	window.run();

	server.stop();
	server_thread.join();

	work_guard.reset();
	tick_timer.cancel();
	io_context.stop();
	io_thread.join();

	return 0;
}
