#include "DataStorage.h"

#include "FormUtil.h"

void DataStorage::InsertConflictField(std::unordered_map<std::string, std::list<std::string>>& a_conflicts, std::string a_field)
{
	if (a_conflicts.contains(a_field)) {
		auto& conflictField = a_conflicts[a_field];
		conflictField.emplace_back(currentFilename);
	} else {
		std::list<std::string> conflictField;
		conflictField.emplace_back(currentFilename);
		a_conflicts.insert({ a_field, conflictField });
	}
}

void DataStorage::InsertConflictInformationRegions(RE::TESForm* a_region, RE::TESForm* a_sound, std::list<std::string> a_fields)
{
	if (!conflictMapRegions.contains(a_region)) {
		std::unordered_map<RE::TESForm*, std::unordered_map<std::string, std::list<std::string>>> insertMap;
		conflictMapRegions[a_region] = insertMap;
	}

	if (!conflictMapRegions[a_region].contains(a_sound)) {
		std::unordered_map<std::string, std::list<std::string>> insertMap;
		conflictMapRegions[a_region][a_sound] = insertMap;
	}
	for (auto field : a_fields)
		InsertConflictField(conflictMapRegions[a_region][a_sound], field);
}

void DataStorage::InsertConflictInformation(RE::TESForm* a_form, std::list<std::string> a_fields)
{
	if (!conflictMap.contains(a_form)) {
		std::unordered_map<std::string, std::list<std::string>> insertMap;
		conflictMap[a_form] = insertMap;
	}

	for (auto field : a_fields)
		InsertConflictField(conflictMap[a_form], field);
}

void DataStorage::LoadConfigs()
{
	std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

	std::set<std::string> configs;
	std::set<std::string> allpluginconfigs;
	auto constexpr folder = R"(Data\)"sv;
	for (const auto& entry : std::filesystem::directory_iterator(folder)) {
		if (entry.exists() && !entry.path().empty() && (entry.path().extension() == ".json"sv || entry.path().extension() == ".jsonc"sv)) {
			const auto path = entry.path().string();
			const auto filename = entry.path().filename().string();
			auto lastindex = filename.find_last_of(".");
			auto rawname = filename.substr(0, lastindex);
			if (rawname.ends_with("_SRD")) {
				const auto path = entry.path().string();
				if (rawname.contains(".es")) {
					allpluginconfigs.insert(path);
				} else {
					configs.insert(path);
				}
			}
		}
	}

	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	logger::info("\nSearched files in {} milliseconds\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());
	begin = std::chrono::steady_clock::now();

	for (auto& file : RE::TESDataHandler::GetSingleton()->files) {
		auto pluginname = file->GetFilename();
		std::set<std::string> pluginconfigs;
		for (const auto& config : allpluginconfigs) {
			auto filename = std::filesystem::path(config).filename().string();
			auto lastindex = filename.find_last_of(".");
			auto rawname = filename.substr(0, lastindex);
			if (filename.rfind(pluginname, 0) == 0) {
				pluginconfigs.insert(config);
			}
		}
		ParseConfigs(pluginconfigs);
	}
	ParseConfigs(configs);

	end = std::chrono::steady_clock::now();
	logger::info("\nParsed configs in {} milliseconds", std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());
	begin = std::chrono::steady_clock::now();

	for (auto& [region, conflictMapRegionsSounds] : conflictMapRegions) {
		if (conflictMapRegionsSounds.size() > 0) {
			logger::info("\n{}", FormUtil::GetIdentifierFromForm(region));
			for (auto& [sound, conflictInformation] : conflictMapRegionsSounds) {
				if (conflictInformation.size() > 0) {
					logger::info("	{}", FormUtil::GetIdentifierFromForm(sound));
					for (auto& [field, files] : conflictInformation) {
						std::string filesString = "";
						for (auto file : files) {
							filesString = " -> " + filesString + file;
						}
						logger::info("		{} {}", field, filesString);
					}
				}
			}
		}
	}

	for (auto& [form, conflictInformation] : conflictMap) {
		if (conflictInformation.size() > 0) {
			logger::info("\n{}", FormUtil::GetIdentifierFromForm(form));
			for (auto& [field, files] : conflictInformation) {
				std::string filesString = "";
				for (auto file : files) {
					filesString = " -> " + filesString + file;
				}
				logger::info("	{} {}", field, filesString);
			}
		}
	}

	end = std::chrono::steady_clock::now();
	logger::info("\nPrinted conflicts in {} milliseconds\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());
}

void DataStorage::ParseConfigs(std::set<std::string>& a_configs)
{
	for (auto config : a_configs) {
		auto filename = std::filesystem::path(config).filename().string();
		logger::info("Parsing {}", filename);
		currentFilename = filename;
		try {
			std::ifstream i(config);
			if (i.good()) {
				json data = json::parse(i, nullptr, true, true);
				i.close();
				RunConfig(data);
			} else {
				std::string errorMessage = std::format("Failed to parse {}\nBad file stream", filename);
				logger::error("{}", errorMessage);
				RE::DebugMessageBox(errorMessage.c_str());
			}
		} catch (const std::exception& exc) {
			std::string errorMessage = std::format("Failed to parse {}\n{}", filename, exc.what());
			logger::error("{}", errorMessage);
			RE::DebugMessageBox(errorMessage.c_str());
		}
	}
}

template <typename T>
T* LookupFormID(std::string a_identifier)
{
	auto form = FormUtil::GetFormFromIdentifier(a_identifier);
	if (form)
		return form->As<T>();
	return nullptr;
}

template <typename T>
T* DataStorage::LookupEditorID(std::string a_editorID)
{
	auto form = RE::TESForm::LookupByEditorID(a_editorID);
	if (form)
		return form->As<T>();
	return nullptr;
}

template <typename T>
T* DataStorage::LookupFormString(std::string a_string)
{
	if (a_string.contains(".es") && a_string.contains("|")) {
		return LookupFormID<T>(a_string);
	}
	return LookupEditorID<T>(a_string);
}

template <typename T>
T* DataStorage::LookupForm(json& a_record)
{
	try {
		auto ret = LookupFormString<T>(a_record["Form"]);
		if (!ret) {
			std::string identifier = a_record["Form"];
			std::string name = typeid(T).name();
			std::string errorMessage = std::format("Form {} of {} does not exist in {}", identifier, name, currentFilename);
			logger::error("{}", errorMessage);
			RE::DebugMessageBox(errorMessage.c_str());
		}
		return ret;
	} catch (const std::exception& exc) {
		std::string errorMessage = std::format("Failed to parse entry in {}\n{}", currentFilename, exc.what());
		logger::error("{}", errorMessage);
		RE::DebugMessageBox(errorMessage.c_str());
	}
	return nullptr;
}

stl::enumeration<RE::TESRegionDataSound::Sound::Flag, std::uint32_t> DataStorage::GetSoundFlags(std::list<std::string> a_flagsList)
{
	stl::enumeration<RE::TESRegionDataSound::Sound::Flag, std::uint32_t> flags;
	int numFlags = 0;
	for (auto flagString : a_flagsList) {
		numFlags++;
		if (flagString == "Pleasant") {
			flags.set(RE::TESRegionDataSound::Sound::Flag::kPleasant);
		} else if (flagString == "Cloudy") {
			flags.set(RE::TESRegionDataSound::Sound::Flag::kCloudy);
		} else if (flagString == "Rainy") {
			flags.set(RE::TESRegionDataSound::Sound::Flag::kRainy);
		} else if (flagString == "Snowy") {
			flags.set(RE::TESRegionDataSound::Sound::Flag::kSnowy);
		} else {
			numFlags--;
		}
	}
	if (!numFlags)
		flags.set(RE::TESRegionDataSound::Sound::Flag::kNone);
	return flags;
}

RE::TESRegionDataSound::Sound* GetOrCreateSound(bool& aout_created, RE::BSTArray<RE::TESRegionDataSound::Sound*> a_sounds, RE::BGSSoundDescriptorForm* a_soundDescriptor)
{
	for (auto sound : a_sounds) {
		if (sound->sound == a_soundDescriptor) {
			aout_created = false;
			return sound;
		}
	}
	aout_created = true;
	auto soundRecord = new RE::TESRegionDataSound::Sound;
	return a_sounds.emplace_back(soundRecord);
}

bool DataStorage::IsModLoaded(std::string a_modname)
{
	static const auto dataHandler = RE::TESDataHandler::GetSingleton();
	if (REL::Module::IsVR()) {
		auto& files = dataHandler->files;
		for (const auto file : files) {
			if (file->GetFilename() == a_modname && file->GetCompileIndex() != 255)
				return true;
		}
		return false;
	}
	return dataHandler->GetLoadedModIndex(a_modname) || dataHandler->GetLoadedLightModIndex(a_modname);
}

void DataStorage::RunConfig(json& a_jsonData)
{
	static const auto dataHandler = RE::TESDataHandler::GetSingleton();
	bool load = true;

	for (auto& record : a_jsonData["Requirements"]) {
		std::string modname = record;
		bool notLoad = false;
		if (modname.ends_with('!')) {
			notLoad = true;
			modname.pop_back();
			if (!IsModLoaded(modname))
				continue;
		} else if (IsModLoaded(modname))
			continue;
		if (notLoad)
			logger::info("	Missing requirement NOT {}", modname);
		else
			logger::info("	Missing requirement {}", modname);
		load = false;
	}

	if (load) {
		for (auto& record : a_jsonData["Region"]) {
			if (auto regn = LookupForm<RE::TESRegion>(record)) {
				RE::TESRegionDataSound* regionDataEntry = nullptr;
				for (auto entry : regn->dataList->regionDataList) {
					if (entry->GetType() == RE::TESRegionData::Type::kSound) {
						const auto regionDataManager = REL::Module::IsVR() ? reinterpret_cast<RE::TESRegionDataManager**>((char*)dataHandler + 0x1580) : dataHandler->regionDataManager;
						if (regionDataManager)
							regionDataEntry = (*regionDataManager)->AsRegionDataSound(entry);
						if (regionDataEntry)
							break;
					}
				}
				if (regionDataEntry) {
					for (auto rdsa : record["RDSA"]) {
						if (rdsa["Sound"] != nullptr; auto sound = LookupFormString<RE::BGSSoundDescriptorForm>(rdsa["Sound"])) {
							bool created;
							std::list<std::string> changes;
							auto soundRecord = GetOrCreateSound(created, regionDataEntry->sounds, sound);
							soundRecord->sound = sound;

							if (rdsa["Flags"] != nullptr) {
								soundRecord->flags = GetSoundFlags(rdsa["Flags"]);
								changes.emplace_back("Flags");
							} else if (created) {
								soundRecord->flags = GetSoundFlags({ "Pleasant", "Cloudy", "Rainy", "Snowy" });
								changes.emplace_back("Flags");
							}
							if (rdsa["Chance"] != nullptr) {
								soundRecord->chance = rdsa["Chance"];
								changes.emplace_back("Chance");
							} else if (created) {
								soundRecord->chance = 0.05f;
								changes.emplace_back("Chance");
							}

							regionDataEntry->sounds.emplace_back(soundRecord);
							InsertConflictInformationRegions(regn, sound, changes);
						}
					}
				} else {
					std::string errorMessage = std::format("RDSA entry does not exist in form {} {:X}", regn->GetFormEditorID(), regn->formID);
					logger::error("{}", errorMessage);
					RE::DebugMessageBox(errorMessage.c_str());
				}
			}
		}

		for (auto& record : a_jsonData["Weapon"]) {
			if (auto weap = LookupForm<RE::TESObjectWEAP>(record)) {
				std::list<std::string> changes;
				if (record["Pick Up"] != nullptr; auto ynam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Pick Up"])) {
					weap->pickupSound = ynam;
					changes.emplace_back("Pick Up");
				}
				if (record["Put Down"] != nullptr; auto znam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Put Down"])) {
					weap->putdownSound = znam;
					changes.emplace_back("Put Down");
				}
				if (record["Impact Data Set"] != nullptr; auto inam = LookupFormString<RE::BGSImpactDataSet>(record["Impact Data Set"])) {
					weap->impactDataSet = inam;
					changes.emplace_back("Impact Data Set");
				}
				if (record["Attack"] != nullptr; auto snam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Attack"])) {
					weap->attackSound = snam;
					changes.emplace_back("Attack");
				}
				if (record["Attack 2D"] != nullptr; auto xnam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Attack 2D"])) {
					weap->attackSound2D = xnam;
					changes.emplace_back("Attack 2D");
				}
				if (record["Attack Loop"] != nullptr; auto nam7 = LookupFormString<RE::BGSSoundDescriptorForm>(record["Attack Loop"])) {
					weap->attackLoopSound = nam7;
					changes.emplace_back("Attack Loop");
				}
				if (record["Attack Fail"] != nullptr; auto tnam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Attack Fail"])) {
					weap->attackFailSound = tnam;
					changes.emplace_back("Attack Fail");
				}
				if (record["Idle"] != nullptr; auto unam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Idle"])) {
					weap->idleSound = unam;
					changes.emplace_back("Idle");
				}
				if (record["Equip"] != nullptr; auto nam9 = LookupFormString<RE::BGSSoundDescriptorForm>(record["Equip"])) {
					weap->equipSound = nam9;
					changes.emplace_back("Equip");
				}
				if (record["Unequip"] != nullptr; auto nam8 = LookupFormString<RE::BGSSoundDescriptorForm>(record["Unequip"])) {
					weap->unequipSound = nam8;
					changes.emplace_back("Unequip");
				}
				InsertConflictInformation(weap, changes);
			}
		}

		for (auto& record : a_jsonData["Magic Effect"]) {
			if (auto mgef = LookupForm<RE::EffectSetting>(record)) {
				std::list<std::string> changes;
				RE::BGSSoundDescriptorForm* slots[6];

				for (int i = 0; i < 6; i++) {
					auto soundID = std::string(magic_enum::enum_name((RE::MagicSystem::SoundID)i));
					soundID = soundID.substr(1, soundID.length() - 1);
					slots[i] = record[soundID] != nullptr ? LookupFormString<RE::BGSSoundDescriptorForm>(record[soundID]) : nullptr;
					if (slots[i])
						changes.emplace_back(soundID);
				}

				stl::enumeration<RE::MagicSystem::SoundID, std::uint32_t> found;
				for (auto sndd : mgef->effectSounds) {
					int i = (int)sndd.id;
					if (slots[i]) {
						sndd.sound = slots[i];
						slots[i] = nullptr;
					}
				}

				for (int i = 0; i < 6; i++) {
					if (slots[i]) {
						RE::EffectSetting::SoundPair soundPair;
						soundPair.id = (RE::MagicSystem::SoundID)i;
						soundPair.sound = slots[i];
						soundPair.pad04 = 0;
						mgef->effectSounds.emplace_back(soundPair);
					}
				}
				InsertConflictInformation(mgef, changes);
			}
		}

		for (auto& record : a_jsonData["Armor Addon"]) {
			if (auto arma = LookupForm<RE::TESObjectARMA>(record)) {
				std::list<std::string> changes;
				if (record["Footstep"] != nullptr; auto sndd = LookupFormString<RE::BGSFootstepSet>(record["Footstep"])) {
					arma->footstepSet = sndd;
					changes.emplace_back("Footstep");
				}
				InsertConflictInformation(arma, changes);
			}
		}

		for (auto& record : a_jsonData["Armor"]) {
			if (auto armo = LookupForm<RE::TESObjectARMO>(record)) {
				std::list<std::string> changes;
				if (record["Pick Up"] != nullptr; auto ynam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Pick Up"])) {
					armo->pickupSound = ynam;
					changes.emplace_back("Pick Up");
				}
				if (record["Put Down"] != nullptr; auto znam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Put Down"])) {
					armo->putdownSound = znam;
					changes.emplace_back("Put Down");
				}
				InsertConflictInformation(armo, changes);
			}
		}

		for (auto& record : a_jsonData["Misc. Item"]) {
			if (auto misc = LookupForm<RE::TESObjectMISC>(record)) {
				std::list<std::string> changes;
				if (record["Pick Up"] != nullptr; auto ynam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Pick Up"])) {
					misc->pickupSound = ynam;
					changes.emplace_back("Pick Up");
				}
				if (record["Put Down"] != nullptr; auto znam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Put Down"])) {
					misc->putdownSound = znam;
					changes.emplace_back("Put Down");
				}
				InsertConflictInformation(misc, changes);
			}
		}

		for (auto& record : a_jsonData["Soul Gem"]) {
			if (auto slgm = LookupForm<RE::TESSoulGem>(record)) {
				std::list<std::string> changes;
				if (record["Pick Up"] != nullptr; auto ynam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Pick Up"])) {
					slgm->pickupSound = ynam;
					changes.emplace_back("Pick Up");
				}
				if (record["Put Down"] != nullptr; auto znam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Put Down"])) {
					slgm->putdownSound = znam;
					changes.emplace_back("Put Down");
				}
				InsertConflictInformation(slgm, changes);
			}
		}
	}
}
