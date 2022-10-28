#pragma once

#include <iostream>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <shared_mutex>
using json = nlohmann::json;


class DataStorage
{
public:
	static DataStorage* GetSingleton()
	{
		static DataStorage avInterface;
		return &avInterface;
	}

	void LoadConfigs();
	void ParseConfigs(std::set<std::string>& a_configs);
	void RunConfig(json& s_jsonData);

	stl::enumeration<RE::TESRegionDataSound::Sound::Flag, std::uint32_t> GetSoundFlags(std::list<std::string> a_input);


private:
	DataStorage() {
	}

};
