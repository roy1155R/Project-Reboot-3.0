#pragma once

// HyperFN additions: the HyperFN admin panel controls the game server through small files next to the game executable
// (the server's working directory). Each server started by the panel gets "-hyperfn=<id>" on its command line and
// its own folder, so several servers can run from one game install:
//   hyperfn\<id>\server.ini              playlist=<path>, minplayers=<n>, busdelay=<seconds>, lategame=0|1, port=<udp>
//                                        playlist, lategame and port are read once at start; the bus values every second
//   hyperfn\<id>\command.txt             "startbus" starts the aircraft now; the file is deleted once handled
//   hyperfn\loadouts\<PlayerName>.txt    character=CID_..., backpack=BID_...; applied when the player's pawn spawns
// Without -hyperfn the files sit directly in hyperfn\ (older panels).

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <windows.h>

namespace HyperFN
{
	// GetTickCount64() time at which the bus auto timer ends (0 = no countdown running).
	extern inline std::atomic<unsigned long long> BusDeadline{ 0 };

	// Bus start asked for off the game thread (1 = admin panel / GUI button); HyperFNBusGameTick starts it on the game thread.
	extern inline std::atomic<int> StartBusRequest{ 0 };

	// Warmup countdown asked for by the admin panel ("countdown" in command.txt), started by HyperFNGameTick.
	extern inline std::atomic<int> StartCountdownRequest{ 0 };

	// Lategame: the zone is put at its lategame phase when the aircraft starts. Until GetTickCount64() passes this, the
	// game's own "start the zone" call (made when the aircraft phase ends) is ignored so the zone does not move on a phase.
	extern inline std::atomic<unsigned long long> ZoneLockUntil{ 0 };

	// Set while HyperFN itself advances the zone, so ZoneLockUntil does not stop it.
	extern inline std::atomic<bool> ZoneForcing{ false };

	inline std::string Trim(const std::string& s)
	{
		auto b = s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos)
			return "";
		auto e = s.find_last_not_of(" \t\r\n");
		return s.substr(b, e - b + 1);
	}

	// hyperfn\<id>\ from "-hyperfn=<id>" on the command line, or hyperfn\ without it.
	inline const std::string& Dir()
	{
		static std::string Value = []
		{
			std::string Cmd = GetCommandLineA();
			auto At = Cmd.find("-hyperfn=");
			if (At == std::string::npos)
				return std::string("hyperfn\\");
			auto Id = Cmd.substr(At + 9, Cmd.find_first_of(" \"", At + 9) - (At + 9));
			for (char c : Id)
				if (!isalnum((unsigned char)c) && c != '-' && c != '_')
					return std::string("hyperfn\\");
			return Id.empty() ? std::string("hyperfn\\") : "hyperfn\\" + Id + "\\";
		}();
		return Value;
	}

	// key=value lines; keys are lowercased, missing file gives an empty map.
	inline std::map<std::string, std::string> ReadKeyValues(const std::string& Path)
	{
		std::map<std::string, std::string> Out;
		std::ifstream File(Path);
		std::string Line;

		while (std::getline(File, Line))
		{
			auto Eq = Line.find('=');
			if (Eq == std::string::npos)
				continue;

			auto Key = Trim(Line.substr(0, Eq));
			for (auto& c : Key)
				c = (char)tolower((unsigned char)c);

			Out[Key] = Trim(Line.substr(Eq + 1));
		}

		return Out;
	}

	inline std::map<std::string, std::string> ReadServerSettings()
	{
		return ReadKeyValues(Dir() + "server.ini");
	}

	// UDP port the game listens on (server.ini port=, default 7777), read once.
	inline int Port()
	{
		static int Value = []
		{
			auto Settings = ReadServerSettings();
			int P = Settings.count("port") ? std::atoi(Settings["port"].c_str()) : 0;
			return P >= 1024 && P <= 65535 ? P : 7777;
		}();
		return Value;
	}

	// True once when hyperfn\<id>\command.txt holds Command; the file is removed so it runs only once.
	inline bool TakeCommand(const std::string& Command)
	{
		std::string Path = Dir() + "command.txt";
		std::ifstream File(Path);
		if (!File)
			return false;

		std::string Content;
		std::getline(File, Content);
		File.close();

		if (Trim(Content) != Command)
			return false;

		std::remove(Path.c_str());
		return true;
	}
}
