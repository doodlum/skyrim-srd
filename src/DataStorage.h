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

	std::string currentFilename = "";
	std::unordered_map<RE::TESForm*, std::unordered_map<RE::TESForm*, std::unordered_map<std::string, std::list<std::string>>>> conflictMapRegions;
	std::unordered_map<RE::TESForm*, std::unordered_map<std::string, std::list<std::string>>> conflictMap;

	void InsertConflictField(std::unordered_map<std::string, std::list<std::string>>& a_conflicts, std::string a_field);

	void InsertConflictInformationRegions(RE::TESForm* a_region, RE::TESForm* a_sound, std::list<std::string> a_fields);
	void InsertConflictInformation(RE::TESForm* a_form, std::list<std::string> a_fields);

	void LoadConfigs();
	void ParseConfigs(std::set<std::string>& a_configs);
	bool IsModLoaded(std::string a_modname);
	void RunConfig(json& s_jsonData);

	stl::enumeration<RE::TESRegionDataSound::Sound::Flag, std::uint32_t> GetSoundFlags(std::list<std::string> a_input);

private:
	DataStorage() {
	}

template <typename T>
	T* LookupEditorID(std::string a_editorID);

	template <typename T>
	T* LookupFormString(json& a_record, std::string a_string);

	template <typename T>
	T* LookupForm(json& a_record);
};
