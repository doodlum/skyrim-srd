#include "DataStorage.h"

#include "FormUtil.h"

void DataStorage::LoadConfigs()
{
	std::set<std::string> configs;
	auto constexpr folder = R"(Data\)"sv;
	for (const auto& entry : std::filesystem::directory_iterator(folder)) {
		if (entry.exists() && !entry.path().empty() && entry.path().extension() == ".json"sv) {
			const auto path = entry.path().string();
			const auto filename = entry.path().filename().string();
			auto lastindex = filename.find_last_of(".");
			auto rawname = filename.substr(0, lastindex);
			if (rawname.ends_with("_SIF") && !rawname.contains(".es")) {
				const auto path = entry.path().string();
				configs.insert(path);
			}
		}
	}

	for (auto& file : RE::TESDataHandler::GetSingleton()->files) {
		auto pluginname = file->GetFilename();

		std::set<std::string> pluginconfigs;
		for (const auto& entry : std::filesystem::directory_iterator(folder)) {
			if (entry.exists() && !entry.path().empty() && entry.path().extension() == ".json"sv) {
				const auto path = entry.path().string();
				const auto filename = entry.path().filename().string();
				auto lastindex = filename.find_last_of(".");
				auto rawname = filename.substr(0, lastindex);
				if (rawname.ends_with("_SIF") && filename.rfind(pluginname, 0) == 0) {
					pluginconfigs.insert(path);
				}
			}
		}

		ParseConfigs(pluginconfigs);
	}

	ParseConfigs(configs);
}

void DataStorage::ParseConfigs(std::set<std::string>& a_configs)
{
	for (auto config : a_configs) {
		auto filename = std::filesystem::path(config).filename().string();
		logger::info("Parsing {}", filename);
		try {
			std::ifstream i(config);
			if (i.good()) {
				json data = json::parse(i, nullptr, true, true);
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
T* LookupEditorID(std::string a_editorID)
{
	auto form = RE::TESForm::LookupByEditorID(a_editorID);
	if (form)
		return form->As<T>();
	return nullptr;
}

template <typename T>
T* LookupFormString(std::string a_string)
{
	if (a_string.contains(".es") && a_string.contains("|")) {
		return LookupFormID<T>(a_string);
	}
	return LookupEditorID<T>(a_string);
}

template <typename T>
T* LookupForm(json& a_record)
{
	if (a_record["Form"] != nullptr) {
		return LookupFormString<T>(a_record["Form"]);
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

void DataStorage::RunConfig(json& a_jsonData)
{
	for (auto& record : a_jsonData["Region"]) {
		if (auto regn = LookupForm<RE::TESRegion>(record)) {
			RE::TESRegionDataSound* regionDataEntry = nullptr;
			for (auto entry : regn->dataList->regionDataList) {
				if (entry->GetType() == RE::TESRegionData::Type::kSound) {
					regionDataEntry = RE::TESDataHandler::GetSingleton()->regionDataManager->AsRegionDataSound(entry);
					if (regionDataEntry)
						break;
				}
			}
			if (regionDataEntry) {
				for (auto rdsa : record["RDSA"]) {
					if (rdsa["Sound"] != nullptr; auto sound = LookupFormString<RE::BGSSoundDescriptorForm>(rdsa["Sound"])) {
						bool created;
						auto soundRecord = GetOrCreateSound(created, regionDataEntry->sounds, sound);
						soundRecord->sound = sound;

						if (rdsa["Flags"] != nullptr)
							soundRecord->flags = GetSoundFlags(rdsa["Flags"]);
						else if (created)
							soundRecord->flags = GetSoundFlags({ "Pleasant", "Cloudy", "Rainy", "Snowy" });

						if (rdsa["Chance"] != nullptr)
							soundRecord->chance = rdsa["Chance"];
						else if (created)
							soundRecord->chance = 0.05f;

						regionDataEntry->sounds.emplace_back(soundRecord);
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
			if (record["Pick Up"] != nullptr; auto ynam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Pick Up"])) 
				weap->pickupSound = ynam;

			if (record["Put Down"] != nullptr; auto znam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Put Down"]))
				weap->putdownSound = znam;

			if (record["Impact Data Set"] != nullptr; auto inam = LookupFormString<RE::BGSImpactDataSet>(record["Impact Data Set"]))
				weap->impactDataSet = inam;

			if (record["Attack"] != nullptr; auto snam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Attack"]))
				weap->attackSound = snam;

			if (record["Attack 2D"] != nullptr; auto xnam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Attack 2D"]))
				weap->attackSound2D = xnam;

			if (record["Attack Loop"] != nullptr; auto nam7 = LookupFormString<RE::BGSSoundDescriptorForm>(record["Attack Loop"]))
				weap->attackLoopSound = nam7;

			if (record["Attack Fail"] != nullptr; auto tnam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Idle"]))
				weap->attackFailSound = tnam;

			if (record["Idle"] != nullptr; auto unam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Unequp"]))
				weap->idleSound = unam;

			if (record["Equip"] != nullptr; auto nam9 = LookupFormString<RE::BGSSoundDescriptorForm>(record["Equip"]))
				weap->equipSound = nam9;

			if (record["Unequp"] != nullptr; auto nam8 = LookupFormString<RE::BGSSoundDescriptorForm>(record["Unequp"]))
				weap->unequipSound = nam8;
		}
	}

	for (auto& record : a_jsonData["Magic Effect"]) {
		if (auto mgef = LookupForm<RE::EffectSetting>(record)) {
			RE::BGSSoundDescriptorForm* slots[6];

			for (int i = 0; i < 6; i++) {
				auto soundID = std::string(magic_enum::enum_name((RE::MagicSystem::SoundID)i));
				soundID = soundID.substr(1, soundID.length() - 1);
				slots[i] = record[soundID] ? LookupForm<RE::BGSSoundDescriptorForm>(record[soundID]) : nullptr;
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
		}
	}

	for (auto& record : a_jsonData["Armor Addon"]) {
		if (auto arma = LookupForm<RE::TESObjectARMA>(record)) {
			if (record["Footstep"] != nullptr; auto sndd = LookupFormString<RE::BGSFootstepSet>(record["Footstep"])) {
				arma->footstepSet = sndd;
			}
		}
	}

	for (auto& record : a_jsonData["Armor"]) {
		if (auto armo = LookupForm<RE::TESObjectARMO>(record)) {
			if (record["Pick Up"] != nullptr; auto ynam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Pick Up"])) {
				armo->pickupSound = ynam;
			}
			if (record["Put Down"] != nullptr; auto znam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Put Down"])) {
				armo->putdownSound = znam;
			}
		}
	}

	for (auto& record : a_jsonData["Misc. Item"]) {
		if (auto misc = LookupForm<RE::TESObjectMISC>(record)) {
			if (record["Pick Up"] != nullptr; auto ynam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Pick Up"])) {
				misc->pickupSound = ynam;
			}
			if (record["Put Down"] != nullptr; auto znam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Put Down"])) {
				misc->putdownSound = znam;
			}
		}
	}

	for (auto& record : a_jsonData["Soul Gem"]) {
		if (auto slgm = LookupForm<RE::TESSoulGem>(record)) {
			if (record["Pick Up"] != nullptr; auto ynam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Pick Up"])) {
				slgm->pickupSound = ynam;
			}
			if (record["Put Down"] != nullptr; auto znam = LookupFormString<RE::BGSSoundDescriptorForm>(record["Put Down"])) {
				slgm->putdownSound = znam;
			}
		}
	}
}
