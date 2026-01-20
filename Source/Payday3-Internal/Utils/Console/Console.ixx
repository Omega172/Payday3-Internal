module;
#include <windows.h>
#include <iostream>
#include <string>
#include <format>
#include <io.h>
#include <fcntl.h>

export module Utils.Console;
import Utils.Logging;

export class Console
{
private:
	FILE* m_pSTDOutDummy = nullptr;
	FILE* m_pSTDErrDummy = nullptr;
	FILE* m_pSTDInDummy = nullptr;

	bool m_bInitalized : 1 = false;
	bool m_bVisible    : 1 = false;

public:
	Console(bool bVisibility, std::string sConsoleTitle = "DEBUG CONSOLE", DWORD dwMode = ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING) noexcept
	{
		if (m_bInitalized)
			return;

		if (!AllocConsole())
		{		
			MessageBoxA(NULL, std::format("Failed to allocate console! Error: {}", GetLastError()).c_str(), "Error", MB_ICONERROR);
			Utils::LogError(std::to_string(GetLastError())); // This code is here only to log the error, if another console is already attached
			return;
		}

		errno_t errSTDOut = freopen_s(&m_pSTDOutDummy, "CONOUT$", "w", stdout);
		if (errSTDOut != NULL)
		{
			Utils::LogError(std::to_string(errSTDOut));
			return;
		}

		errno_t errSTDErr = freopen_s(&m_pSTDErrDummy, "CONOUT$", "w", stderr);
		if (errSTDErr != NULL)
		{
			Utils::LogError(std::to_string(errSTDErr));
			return;
		}

		errno_t errSTDIn = freopen_s(&m_pSTDInDummy, "CONIN$", "w", stdin);
		if (errSTDIn != NULL)
		{
			Utils::LogError(std::to_string(errSTDIn));
			return;
		}

		std::cout.clear();
		std::cerr.clear();
		std::cin.clear();
		
		SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), dwMode);

		SetTitle(sConsoleTitle);
		SetVisibility(bVisibility);

		m_bInitalized = true;
		return;
	}

	~Console() noexcept { Destroy(); }

	void Destroy()
	{
		if (!m_bInitalized)
			return;

		if (m_pSTDOutDummy)
			fclose(m_pSTDOutDummy);

		if (m_pSTDErrDummy)
			fclose(m_pSTDErrDummy);

		if (m_pSTDInDummy)
			fclose(m_pSTDInDummy);

		if (!FreeConsole())
			Utils::LogError(std::to_string(GetLastError()));

		m_bInitalized = false;
		return;
	}

	void SetTitle(std::string sTitle) { SetConsoleTitleA(sTitle.c_str()); }

	bool GetVisibility() { return this->m_bVisible; }

	void SetVisibility(bool bVisibility)
	{
		m_bVisible = bVisibility;
		ShowWindow(GetConsoleWindow(), (bVisibility) ? SW_SHOW : SW_HIDE);
		return;
	}

	void ToggleVisibility()
	{
		SetVisibility(!m_bVisible);
		return;
	}

	void SetMode(DWORD dwMode)
	{
		SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), dwMode);
		return;
	}
};