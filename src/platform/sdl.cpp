#define _AMD64_

#include "curl/curl.h"
#include <glew/include/GL/glew.h>
#include <sdl/include/SDL.h>
//#undef main

#include "render/glvm.h"
#include "load.h"

#include <thread>
#include "physics.h"
#include "loop.h"
#include "settings.h"
#if _WIN32
#include <Windows.h>
#include <DbgHelp.h>
#include <ShellScalingAPI.h>
#pragma comment(lib, "Shcore.lib")
#include "game/menu.h"
#endif
#include <time.h>
#include "lodepng/lodepng.h"
#include "asset/version.h"
#include <sstream>
#include "data/json.h"
#include "mersenne/mersenne-twister.h"

#if _WIN32
extern "C"
{
	_declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace VI
{

	namespace platform
	{

		u64 timestamp()
		{
			time_t t;
			::time(&t);
			return u64(t);
		}

		r64 time()
		{
			return (SDL_GetTicks() / 1000.0);
		}

		void sleep(r32 time)
		{
			SDL_Delay(u32(time * 1000.0f));
		}

	}

	SDL_Window* window = 0;
	SDL_GameController* controllers[MAX_GAMEPADS] = {};
	Gamepad::Type controller_types[MAX_GAMEPADS] = {};
	SDL_Haptic* haptics[MAX_GAMEPADS] = {};

	void refresh_controllers()
	{
		for (s32 i = 0; i < MAX_GAMEPADS; i++)
		{
			if (haptics[i])
			{
				SDL_HapticClose(haptics[i]);
				haptics[i] = nullptr;
			}

			if (controllers[i])
			{
				SDL_GameControllerClose(controllers[i]);
				controllers[i] = nullptr;
			}
			controller_types[i] = Gamepad::Type::None;
		}

		for (s32 i = 0; i < SDL_NumJoysticks(); i++)
		{
			if (SDL_IsGameController(i))
			{
				controllers[i] = SDL_GameControllerOpen(i);
				const char* name = SDL_GameControllerName(controllers[i]);
				if (name && strstr(name, "DualShock"))
					controller_types[i] = Gamepad::Type::Playstation;
				else
					controller_types[i] = Gamepad::Type::Xbox;

				SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controllers[i]);

				if (SDL_JoystickIsHaptic(joystick))
				{
					haptics[i] = SDL_HapticOpenFromJoystick(joystick);
					if (SDL_HapticRumbleInit(haptics[i])) // failed
					{
						SDL_HapticClose(haptics[i]);
						haptics[i] = nullptr;
					}
				}
			}
		}
	}

	b8 vsync_set(b8 vsync)
	{
		if (SDL_GL_SetSwapInterval(vsync ? 1 : 0) != 0)
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Failed to set OpenGL swap interval", SDL_GetError(), nullptr);
			return false;
		}
		return true;
	}

#if _WIN32
	void create_mini_dump(EXCEPTION_POINTERS* pep, const char* path)
	{
		typedef BOOL(*PDUMPFN)
		(
			HANDLE hProcess,
			DWORD ProcessId,
			HANDLE hFile,
			MINIDUMP_TYPE DumpType,
			PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
			PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
			PMINIDUMP_CALLBACK_INFORMATION CallbackParam
		);

		HANDLE hFile = CreateFile(path, GENERIC_READ | GENERIC_WRITE,
			0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		HMODULE lib = ::LoadLibrary("DbgHelp.dll");
		PDUMPFN pFn = PDUMPFN(GetProcAddress(lib, "MiniDumpWriteDump"));

		if (hFile && hFile != INVALID_HANDLE_VALUE)
		{
			// Create the minidump 

			MINIDUMP_EXCEPTION_INFORMATION mdei;
			mdei.ThreadId = GetCurrentThreadId();
			mdei.ExceptionPointers = pep;
			mdei.ClientPointers = TRUE;

			BOOL rv = (*pFn)(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, (pep != 0) ? &mdei : 0, 0, 0);

			CloseHandle(hFile);
		}
	}

	void mini_dump_name(std::string* name, DWORD64 rip)
	{
		std::stringstream builder;
		builder << BUILD_ID;
		time_t timev = time(NULL);
		struct tm * now = localtime(&timev);
		builder << "-" << rip << "-" << now->tm_year + 1900 << "-" << now->tm_mon + 1 << "-" << now->tm_mday << "-" << now->tm_hour << "-" << now->tm_min << "-" << now->tm_sec << "-" << mersenne::rand() << ".dmp";
		*name = builder.str();
	}

	void mini_dump_path(std::string* path, DWORD64 rip)
	{
		std::stringstream builder;
		builder << Loader::data_directory;
		std::string name;
		mini_dump_name(&name, rip);
		builder << name;
		*path = builder.str();
	}

	size_t mini_dump_read_callback(void* ptr, size_t size, size_t nmemb, void* stream)
	{
		return fread(ptr, size, nmemb, (FILE*)(stream));
	}

	LONG WINAPI crash_reporter(PEXCEPTION_POINTERS exc_info)
	{
		std::string path;
		mini_dump_path(&path, exc_info->ContextRecord->Rip);

		create_mini_dump(exc_info, path.c_str());

		{
			FILE* fd = fopen(path.c_str(), "rb");
			if (!fd)
				return EXCEPTION_EXECUTE_HANDLER;

			struct stat file_info;
			if (fstat(fileno(fd), &file_info))
				return EXCEPTION_EXECUTE_HANDLER;

			if (CURL* curl = curl_easy_init())
			{
				curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

				{
					std::stringstream url;
#if RELEASE_BUILD
					url << "https://" << Settings::master_server << "/crash_dump";
#else
					url << "http://[" << Settings::master_server << "]:" << NET_MASTER_HTTP_PORT << "/crash_dump";
#endif
					curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
				}

				curl_mime* form = curl_mime_init(curl);
				curl_mimepart* field = curl_mime_addpart(form);
				curl_mime_name(field, "crash_dump", CURL_ZERO_TERMINATED);
				curl_mime_filedata(field, path.c_str());
				curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);

				struct curl_slist* headers = nullptr;
				headers = curl_slist_append(headers, "Expect:"); // initialize custom header list stating that Expect: 100-continue is not wanted
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

				curl_easy_perform(curl);

				curl_easy_cleanup(curl);

				curl_mime_free(form);

				curl_slist_free_all(headers);
			}
		}

		return EXCEPTION_EXECUTE_HANDLER;
	}
#endif

	s32 proc()
	{
#if _WIN32
		SetUnhandledExceptionFilter(crash_reporter);
#endif

#if _WIN32
		SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
#endif

#if defined(__APPLE__)
		SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "1");
#endif

		// initialize SDL
		if (SDL_Init(
			SDL_INIT_VIDEO
			| SDL_INIT_EVENTS
			| SDL_INIT_GAMECONTROLLER
			| SDL_INIT_HAPTIC
			| SDL_INIT_TIMER
			| SDL_INIT_JOYSTICK
		) < 0)
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SDL Error", SDL_GetError(), nullptr);
			return 1;
		}

		Loader::data_directory = SDL_GetPrefPath("HelveticaScenario", "Deceiver");

		{
			const char* build_file = "build.txt";
			cJSON* json = Json::load(build_file);
			Game::language = Json::get_string(json, "language", "en");

			const char* auth_type = Json::get_string(json, "auth", "");
			if (strcmp(auth_type, "gamejolt") == 0)
			{
				Game::auth_type = Net::Master::AuthType::GameJolt;
				FILE* f = fopen(".gj-credentials", "r");
				if (f)
				{
					char buffer[512];
					fgets(buffer, 512, f); // version
					fgets(Settings::gamejolt_username, MAX_PATH_LENGTH, f); // username
					Settings::gamejolt_username[strcspn(Settings::gamejolt_username, "\r\n")] = 0;
					fgets(Settings::gamejolt_token, MAX_AUTH_KEY, f); // token
					Settings::gamejolt_token[strcspn(Settings::gamejolt_token, "\r\n")] = 0;
					fclose(f);
				}
			}
			else if (strcmp(auth_type, "itch") == 0)
			{
				Game::auth_type = Net::Master::AuthType::Itch;
				const char* itch_api_key = getenv("ITCHIO_API_KEY");
				if (itch_api_key)
				{
					Game::auth_type = Net::Master::AuthType::Itch;
					Game::auth_key_length = vi_max(0, vi_min(s32(strlen(itch_api_key)), MAX_AUTH_KEY));
					memcpy(Game::auth_key, itch_api_key, Game::auth_key_length);
				}
			}
			else if (strcmp(auth_type, "steam") == 0)
				Game::auth_type = Net::Master::AuthType::Steam;
			else
				Game::auth_type = Net::Master::AuthType::None;

			// don't free the json; keep the strings in memory
		}

		SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

		{
			Array<DisplayMode> modes;
			for (s32 i = SDL_GetNumDisplayModes(0) - 1; i >= 0; i--)
			{
				SDL_DisplayMode mode;
				SDL_GetDisplayMode(0, i, &mode);

				// check for duplicates; could have multiple display modes with same resolution but different refresh rate
				b8 unique = true;
				for (s32 j = 0; j < modes.length; j++)
				{
					const DisplayMode& other = modes[j];
					if (other.width == mode.w && other.height == mode.h)
					{
						unique = false;
						break;
					}
				}
				
				if (unique)
					modes.add({ mode.w, mode.h });
			}

			DisplayMode default_display_mode;
#if defined(__APPLE__)
			{
				// default to windowed mode on mac
				// find largest display mode smaller than the current one
				SDL_DisplayMode current_mode;
				if (SDL_GetDesktopDisplayMode(0, &current_mode))
				{
					
				}

				for (s32 i = 0; i < modes.length; i++)
				{
					const DisplayMode& m = modes[i];
					if (m.width == current_mode.w && m.height == current_mode.h)
					{
						default_display_mode = modes[vi_max(0, i - 1)];
						break;
					}
				}
			}
#else
			{
				SDL_DisplayMode current_mode;
				if (SDL_GetDesktopDisplayMode(0, &current_mode))
				{
					SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Failed to get desktop display mode", SDL_GetError(), nullptr);
					return 1;
				}
				default_display_mode = { current_mode.w, current_mode.h };
			}
#endif

			Loader::settings_load(modes, default_display_mode);
		}
		
		u32 window_mode;
		switch (Settings::window_mode)
		{
			case WindowMode::Windowed:
				window_mode = 0;
				break;
			case WindowMode::Fullscreen:
				window_mode = SDL_WINDOW_FULLSCREEN;
				break;
			case WindowMode::Borderless:
				window_mode = SDL_WINDOW_BORDERLESS;
				break;
			default:
			{
				window_mode = 0;
				vi_assert(false);
				break;
			}
		}
		window = SDL_CreateWindow
		(
			"DECEIVER",
			SDL_WINDOWPOS_CENTERED,
			SDL_WINDOWPOS_CENTERED,
			Settings::display().width, Settings::display().height,
			SDL_WINDOW_OPENGL
			| SDL_WINDOW_SHOWN
			| SDL_WINDOW_INPUT_FOCUS
			| SDL_WINDOW_MOUSE_FOCUS
			| SDL_WINDOW_ALLOW_HIGHDPI
			| window_mode
		);

		// open a window and create its OpenGL context
		if (!window)
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Failed to open SDL window", SDL_GetError(), nullptr);
			return 1;
		}

		SDL_GLContext context = SDL_GL_CreateContext(window);
		if (!context)
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Failed to create GL context", SDL_GetError(), nullptr);
			return 1;
		}

		if (!vsync_set(Settings::vsync))
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Failed to set vsync", SDL_GetError(), nullptr);
			return 1;
		}

		{
			glewExperimental = true; // needed for core profile

			GLenum glew_result = glewInit();
			if (glew_result != GLEW_OK)
			{
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GLEW init failed", (const char*)(glewGetErrorString(glew_result)), nullptr);
				return 1;
			}
		}

		{
			// cursor
			const char* cursor[] =
			{
				"           .......           ",
				"           .......           ",
				"           ..+++..           ",
				"           ..+++..           ",
				"           ..+++..           ",
				"           ..+++..           ",
				"           ..+++..           ",
				"           ..+++..           ",
				"           ..+++..           ",
				"           ..+++..           ",
				".............+++.............",
				".............+++.............",
				"..+++++++++++++++++++++++++..",
				"..+++++++++++++++++++++++++..",
				"..+++++++++++++++++++++++++..",
				".............+++.............",
				".............+++.............",
				"           ..+++..           ",
				"           ..+++..           ",
				"           ..+++..           ",
				"           ..+++..           ",
				"           ..+++..           ",
				"           ..+++..           ",
				"           ..+++..           ",
				"           ..+++..           ",
				"           ..+++..           ",
				"           ..+++..           ",
				"           .......           ",
				"           .......           ",
			};

			u8 data[4 * 32] = {};
			u8 mask[4 * 32] = {};
			for (s32 y = 0; y < 29; y++)
			{
				for (s32 x = 0; x < 29; x++)
				{
					s32 pixel = y * 32 + x;
					s32 index = pixel / 8;
					s32 bit = 7 - (pixel - (index * 8));
					switch (cursor[y][x])
					{
						case '.':
							data[index] |= 1 << bit;
							mask[index] |= 1 << bit;
							break;
						case '+':
							mask[index] |= 1 << bit;
						default:
							break;
					}
				}
			}
			SDL_SetCursor(SDL_CreateCursor(data, mask, 32, 32, 14, 16));
		}

		{
			const char* error;
			Game::PreinitResult pre_init_result = Game::pre_init(&error);
			if (pre_init_result == Game::PreinitResult::Failure)
			{
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DECEIVER Error", error, nullptr);
				return 1;
			}
			else if (pre_init_result == Game::PreinitResult::Restarting)
				return 0;
		}

		DisplayMode resolution_current = Settings::display();
		WindowMode resolution_current_window_mode = Settings::window_mode;
		b8 resolution_current_vsync = Settings::vsync;

		glGetError(); // clear initial error caused by GLEW

		render_init();

		// launch threads

		Sync<LoopSync> render_sync;

		LoopSwapper swapper_render_update = render_sync.swapper(0);
		LoopSwapper swapper_render = render_sync.swapper(1);

		Sync<PhysicsSync, 1> physics_sync;

		PhysicsSwapper swapper_physics = physics_sync.swapper();
		PhysicsSwapper swapper_physics_update = physics_sync.swapper();

		std::thread thread_physics(Physics::loop, &swapper_physics);

		std::thread thread_update(Loop::loop, &swapper_render_update, &swapper_physics_update);

		std::thread thread_ai(AI::loop);

		LoopSync* sync = swapper_render.get();

		b8 has_focus = true;

		SDL_PumpEvents();

		KeyCode keymap[s32(KeyCode::count)];

		{
			SDL_Keycode default_keymap[SDL_NUM_SCANCODES] =
			{
				0, 0, 0, 0,
				'a',
				'b',
				'c',
				'd',
				'e',
				'f',
				'g',
				'h',
				'i',
				'j',
				'k',
				'l',
				'm',
				'n',
				'o',
				'p',
				'q',
				'r',
				's',
				't',
				'u',
				'v',
				'w',
				'x',
				'y',
				'z',
				'1',
				'2',
				'3',
				'4',
				'5',
				'6',
				'7',
				'8',
				'9',
				'0',
				SDLK_RETURN,
				SDLK_ESCAPE,
				SDLK_BACKSPACE,
				SDLK_TAB,
				SDLK_SPACE,
				'-',
				'=',
				'[',
				']',
				'\\',
				'#',
				';',
				'\'',
				'`',
				',',
				'.',
				'/',
				SDLK_CAPSLOCK,
				SDLK_F1,
				SDLK_F2,
				SDLK_F3,
				SDLK_F4,
				SDLK_F5,
				SDLK_F6,
				SDLK_F7,
				SDLK_F8,
				SDLK_F9,
				SDLK_F10,
				SDLK_F11,
				SDLK_F12,
				SDLK_PRINTSCREEN,
				SDLK_SCROLLLOCK,
				SDLK_PAUSE,
				SDLK_INSERT,
				SDLK_HOME,
				SDLK_PAGEUP,
				SDLK_DELETE,
				SDLK_END,
				SDLK_PAGEDOWN,
				SDLK_RIGHT,
				SDLK_LEFT,
				SDLK_DOWN,
				SDLK_UP,
				SDLK_NUMLOCKCLEAR,
				SDLK_KP_DIVIDE,
				SDLK_KP_MULTIPLY,
				SDLK_KP_MINUS,
				SDLK_KP_PLUS,
				SDLK_KP_ENTER,
				SDLK_KP_1,
				SDLK_KP_2,
				SDLK_KP_3,
				SDLK_KP_4,
				SDLK_KP_5,
				SDLK_KP_6,
				SDLK_KP_7,
				SDLK_KP_8,
				SDLK_KP_9,
				SDLK_KP_0,
				SDLK_KP_PERIOD,
				0,
				SDLK_APPLICATION,
				SDLK_POWER,
				SDLK_KP_EQUALS,
				SDLK_F13,
				SDLK_F14,
				SDLK_F15,
				SDLK_F16,
				SDLK_F17,
				SDLK_F18,
				SDLK_F19,
				SDLK_F20,
				SDLK_F21,
				SDLK_F22,
				SDLK_F23,
				SDLK_F24,
				SDLK_EXECUTE,
				SDLK_HELP,
				SDLK_MENU,
				SDLK_SELECT,
				SDLK_STOP,
				SDLK_AGAIN,
				SDLK_UNDO,
				SDLK_CUT,
				SDLK_COPY,
				SDLK_PASTE,
				SDLK_FIND,
				SDLK_MUTE,
				SDLK_VOLUMEUP,
				SDLK_VOLUMEDOWN,
				0, 0, 0,
				SDLK_KP_COMMA,
				SDLK_KP_EQUALSAS400,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				SDLK_ALTERASE,
				SDLK_SYSREQ,
				SDLK_CANCEL,
				SDLK_CLEAR,
				SDLK_PRIOR,
				SDLK_RETURN2,
				SDLK_SEPARATOR,
				SDLK_OUT,
				SDLK_OPER,
				SDLK_CLEARAGAIN,
				SDLK_CRSEL,
				SDLK_EXSEL,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				SDLK_KP_00,
				SDLK_KP_000,
				SDLK_THOUSANDSSEPARATOR,
				SDLK_DECIMALSEPARATOR,
				SDLK_CURRENCYUNIT,
				SDLK_CURRENCYSUBUNIT,
				SDLK_KP_LEFTPAREN,
				SDLK_KP_RIGHTPAREN,
				SDLK_KP_LEFTBRACE,
				SDLK_KP_RIGHTBRACE,
				SDLK_KP_TAB,
				SDLK_KP_BACKSPACE,
				SDLK_KP_A,
				SDLK_KP_B,
				SDLK_KP_C,
				SDLK_KP_D,
				SDLK_KP_E,
				SDLK_KP_F,
				SDLK_KP_XOR,
				SDLK_KP_POWER,
				SDLK_KP_PERCENT,
				SDLK_KP_LESS,
				SDLK_KP_GREATER,
				SDLK_KP_AMPERSAND,
				SDLK_KP_DBLAMPERSAND,
				SDLK_KP_VERTICALBAR,
				SDLK_KP_DBLVERTICALBAR,
				SDLK_KP_COLON,
				SDLK_KP_HASH,
				SDLK_KP_SPACE,
				SDLK_KP_AT,
				SDLK_KP_EXCLAM,
				SDLK_KP_MEMSTORE,
				SDLK_KP_MEMRECALL,
				SDLK_KP_MEMCLEAR,
				SDLK_KP_MEMADD,
				SDLK_KP_MEMSUBTRACT,
				SDLK_KP_MEMMULTIPLY,
				SDLK_KP_MEMDIVIDE,
				SDLK_KP_PLUSMINUS,
				SDLK_KP_CLEAR,
				SDLK_KP_CLEARENTRY,
				SDLK_KP_BINARY,
				SDLK_KP_OCTAL,
				SDLK_KP_DECIMAL,
				SDLK_KP_HEXADECIMAL,
				0, 0,
				SDLK_LCTRL,
				SDLK_LSHIFT,
				SDLK_LALT,
				SDLK_LGUI,
				SDLK_RCTRL,
				SDLK_RSHIFT,
				SDLK_RALT,
				SDLK_RGUI,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				SDLK_MODE,
				SDLK_AUDIONEXT,
				SDLK_AUDIOPREV,
				SDLK_AUDIOSTOP,
				SDLK_AUDIOPLAY,
				SDLK_AUDIOMUTE,
				SDLK_MEDIASELECT,
				SDLK_WWW,
				SDLK_MAIL,
				SDLK_CALCULATOR,
				SDLK_COMPUTER,
				SDLK_AC_SEARCH,
				SDLK_AC_HOME,
				SDLK_AC_BACK,
				SDLK_AC_FORWARD,
				SDLK_AC_STOP,
				SDLK_AC_REFRESH,
				SDLK_AC_BOOKMARKS,
				SDLK_BRIGHTNESSDOWN,
				SDLK_BRIGHTNESSUP,
				SDLK_DISPLAYSWITCH,
				SDLK_KBDILLUMTOGGLE,
				SDLK_KBDILLUMDOWN,
				SDLK_KBDILLUMUP,
				SDLK_EJECT,
				SDLK_SLEEP,
			};

			for (s32 i = 0; i < s32(KeyCode::count); i++)
			{
				keymap[i] = KeyCode::None;

				SDL_Keycode keycode = SDL_GetKeyFromScancode(SDL_Scancode(i));
				for (s32 j = 0; j < s32(KeyCode::count); j++)
				{
					if (default_keymap[j] == keycode)
					{
						keymap[i] = KeyCode(j);
						break;
					}
				}
			}
		}

		b8 cursor_visible = true;

		const u8* sdl_keys = SDL_GetKeyboardState(0);

		refresh_controllers();

		while (true)
		{
			{
				// display mode
				const DisplayMode& desired = sync->display_mode;
				if (desired.width != 0) // we're getting actual valid data from the update thread
				{
					if (sync->vsync != resolution_current_vsync)
					{
						if (!vsync_set(sync->vsync))
						{
							SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Failed to set vsync", SDL_GetError(), nullptr);
							return 1;
						}
						resolution_current_vsync = sync->vsync;
					}

					if (desired.width != resolution_current.width || desired.height != resolution_current.height || sync->window_mode != resolution_current_window_mode)
					{
						SDL_DisplayMode new_mode = {};
						for (s32 i = 0; i < SDL_GetNumDisplayModes(0); i++)
						{
							SDL_DisplayMode m;
							SDL_GetDisplayMode(0, i, &m);
							if (m.w == desired.width && m.h == desired.height && m.refresh_rate > new_mode.refresh_rate)
								new_mode = m;
						}

						vi_assert(new_mode.w == desired.width && new_mode.h == desired.height);
						switch (sync->window_mode)
						{
							case WindowMode::Windowed:
							{
								SDL_SetWindowFullscreen(window, 0);
								SDL_SetWindowBordered(window, SDL_TRUE);
								SDL_SetWindowSize(window, new_mode.w, new_mode.h);
								SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
								break;
							}
							case WindowMode::Fullscreen:
							{
								SDL_SetWindowSize(window, new_mode.w, new_mode.h);
								SDL_SetWindowDisplayMode(window, &new_mode);
								SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
								break;
							}
							case WindowMode::Borderless:
							{
								SDL_SetWindowFullscreen(window, 0);
								SDL_SetWindowBordered(window, SDL_FALSE);
								SDL_SetWindowSize(window, new_mode.w, new_mode.h);
								SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
								break;
							}
							default:
								vi_assert(false);
								break;
						}

						resolution_current = desired;
						resolution_current_window_mode = sync->window_mode;
					}
				}
			}

			if (sync->minimize)
			{
				sync->minimize = false;
				SDL_MinimizeWindow(window);
			}

			render(sync);

			// swap buffers
			SDL_GL_SwapWindow(window);

			SDL_PumpEvents();

			sync->input.keys.clear();
			for (s32 i = 0; i < s32(KeyCode::count); i++)
			{
				if (sdl_keys[i])
				{
					KeyCode code = keymap[i];
					if (code != KeyCode::None)
						sync->input.keys.set(s32(code), true);
				}
			}

#if DEBUG
			// screenshot
			if (sync->input.keys.get(s32(KeyCode::F11)))
			{
				s32 w = Settings::display().width;
				s32 h = Settings::display().height;
				std::vector<unsigned char> data;
				data.resize(w * h * 4);

				{
					glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
					glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, &data[0]);
					GLenum error;
					if ((error = glGetError()) != GL_NO_ERROR)
					{
						vi_debug("GL error: %u", error);
						vi_debug_break();
					}
				}

				// flip image
				{
					vi_assert(h % 2 == 0);
					s32 y2 = h - 1;
					for (s32 y = 0; y < h / 2; y++)
					{
						for (s32 x = 0; x < w; x++)
						{
							u32* i = (u32*)(&data[(y * w + x) * 4]);
							u32* j = (u32*)(&data[(y2 * w + x) * 4]);
							u32 tmp = *i;
							*i = *j;
							*j = tmp;
						}
						y2--;
					}
				}

				lodepng::encode("screen.png", data, w, h);
			}
#endif

			b8 just_gained_focus = false;

			SDL_Event sdl_event;
			while (SDL_PollEvent(&sdl_event))
			{
				if (sdl_event.type == SDL_QUIT)
					sync->quit = true;
				else if (sdl_event.type == SDL_MOUSEWHEEL)
				{ 
					b8 up = sdl_event.wheel.y > 0;
					if (sdl_event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
						up = !up;
					if (up)
						sync->input.keys.set(s32(KeyCode::MouseWheelUp), true);
					else
						sync->input.keys.set(s32(KeyCode::MouseWheelDown), true);
				} 
				else if (sdl_event.type == SDL_JOYDEVICEADDED
					|| sdl_event.type == SDL_JOYDEVICEREMOVED)
					refresh_controllers();
				else if (sdl_event.type == SDL_WINDOWEVENT)
				{
					if (sdl_event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
					{
						has_focus = true;
						just_gained_focus = true;
					}
					else if (sdl_event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
						has_focus = false;
				}
			}

			sync->input.focus = has_focus;

			{
				s32 mouse_buttons;
				if (sync->input.cursor_visible)
				{
					if (!cursor_visible)
					{
						if (SDL_SetRelativeMouseMode(SDL_FALSE) != 0)
						{
							SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Failed to set relative mouse mode", SDL_GetError(), nullptr);
							return 1;
						}
						cursor_visible = true;
					}

					s32 mouse_x;
					s32 mouse_y;
					mouse_buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
					sync->input.cursor = Vec2(mouse_x, resolution_current.height - mouse_y);
				}
				else
				{
					b8 just_enabled_relative = false;
					if (cursor_visible)
					{
						if (SDL_SetRelativeMouseMode(SDL_TRUE) != 0)
						{
							SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Failed to set relative mouse mode", SDL_GetError(), nullptr);
							return 1;
						}
						cursor_visible = false;
						just_enabled_relative = true;
					}

					s32 mouse_x;
					s32 mouse_y;
					mouse_buttons = SDL_GetRelativeMouseState(&mouse_x, &mouse_y);
					if (just_enabled_relative || just_gained_focus)
						sync->input.mouse_relative = Vec2::zero;
					else
						sync->input.mouse_relative = Vec2(mouse_x, mouse_y);
				}

				sync->input.keys.set(s32(KeyCode::MouseLeft), mouse_buttons & (1 << 0));
				sync->input.keys.set(s32(KeyCode::MouseMiddle), mouse_buttons & (1 << 1));
				sync->input.keys.set(s32(KeyCode::MouseRight), mouse_buttons & (1 << 2));
			}

			s32 active_gamepads = 0;
			for (s32 i = 0; i < MAX_GAMEPADS; i++)
			{
				SDL_GameController* controller = controllers[i];
				Gamepad* gamepad = &sync->input.gamepads[i];
				gamepad->type = controller_types[i];
				gamepad->btns = 0;
				if (gamepad->type == Gamepad::Type::None)
				{
					gamepad->left_x = 0.0f;
					gamepad->left_y = 0.0f;
					gamepad->right_x = 0.0f;
					gamepad->right_y = 0.0f;
					gamepad->left_trigger = 0.0f;
					gamepad->right_trigger = 0.0f;
					gamepad->rumble = 0.0f;
				}
				else
				{
					gamepad->left_x = r32(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX)) / 32767.0f;
					gamepad->left_y = r32(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY)) / 32767.0f;
					gamepad->right_x = r32(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX)) / 32767.0f;
					gamepad->right_y = r32(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY)) / 32767.0f;
					gamepad->left_trigger = r32(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT)) / 32767.0f;
					gamepad->right_trigger = r32(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) / 32767.0f;
					if (gamepad->left_trigger > 0.1f)
						gamepad->btns |= 1 << s32(Gamepad::Btn::LeftTrigger);
					if (gamepad->right_trigger > 0.1f)
						gamepad->btns |= 1 << s32(Gamepad::Btn::RightTrigger);
					if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))
						gamepad->btns |= 1 << s32(Gamepad::Btn::LeftShoulder);
					if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
						gamepad->btns |= 1 << s32(Gamepad::Btn::RightShoulder);
					if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSTICK))
						gamepad->btns |= 1 << s32(Gamepad::Btn::LeftClick);
					if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSTICK))
						gamepad->btns |= 1 << s32(Gamepad::Btn::RightClick);
					if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A))
						gamepad->btns |= 1 << s32(Gamepad::Btn::A);
					if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B))
						gamepad->btns |= 1 << s32(Gamepad::Btn::B);
					if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X))
						gamepad->btns |= 1 << s32(Gamepad::Btn::X);
					if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y))
						gamepad->btns |= 1 << s32(Gamepad::Btn::Y);
					if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK))
						gamepad->btns |= 1 << s32(Gamepad::Btn::Back);
					if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START))
						gamepad->btns |= 1 << s32(Gamepad::Btn::Start);
					if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP))
						gamepad->btns |= 1 << s32(Gamepad::Btn::DUp);
					if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
						gamepad->btns |= 1 << s32(Gamepad::Btn::DDown);
					if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
						gamepad->btns |= 1 << s32(Gamepad::Btn::DLeft);
					if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
						gamepad->btns |= 1 << s32(Gamepad::Btn::DRight);
					if (gamepad->rumble > 0.0f)
						SDL_HapticRumblePlay(haptics[i], gamepad->rumble, 33);
					gamepad->rumble = 0.0f;
					active_gamepads++;
				}
			}

			b8 quit = sync->quit;

			sync = swapper_render.swap<SwapType::Read>();

			if (quit || sync->quit)
				break;
		}

		AI::quit();

		thread_update.join();
		thread_physics.join();
		thread_ai.join();

		SDL_GL_DeleteContext(context);
		SDL_DestroyWindow(window);

		// SDL sucks
		for (s32 i = 0; i < MAX_GAMEPADS; i++)
		{
			if (haptics[i])
				SDL_HapticClose(haptics[i]);
		}

		SDL_Quit();

		return 0;
	}

}

int main(int argc, char** argv)
{
	return VI::proc();
}
