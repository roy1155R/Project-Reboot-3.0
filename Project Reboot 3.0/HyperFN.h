#pragma once

// HyperFN additions: the HyperFN admin panel controls the game server through small files next to the game executable
// (the server's working directory):
//   hyperfn\server.ini                   playlist=<path>, minplayers=<n>, busdelay=<seconds>
//                                        playlist is read once at start; the bus values are re-read every second
//   hyperfn\command.txt                  "startbus" starts the aircraft now; the file is deleted once handled
//   hyperfn\loadouts\<PlayerName>.txt    character=CID_..., backpack=BID_...; applied when the player's pawn spawns

#include <atomic>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>

namespace HyperFN
{
	// GetTickCount64() time at which the bus auto timer ends (0 = no countdown running).
	extern inline std::atomic<unsigned long long> BusDeadline{ 0 };

	inline std::string Trim(const std::string& s)
	{
		auto b = s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos)
			return "";
		auto e = s.find_last_not_of(" \t\r\n");
		return s.substr(b, e - b + 1);
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
		return ReadKeyValues("hyperfn\\server.ini");
	}

	// True once when hyperfn\command.txt holds Command; the file is removed so it runs only once.
	inline bool TakeCommand(const std::string& Command)
	{
		std::ifstream File("hyperfn\\command.txt");
		if (!File)
			return false;

		std::string Content;
		std::getline(File, Content);
		File.close();

		if (Trim(Content) != Command)
			return false;

		std::remove("hyperfn\\command.txt");
		return true;
	}
}
