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
			if (rawname.ends_with("_SIF") && !rawname.contains(".esm") && !rawname.contains(".esp") && !rawname.contains(".esl")) {
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
T* LookupForm(auto& a_record)
{
	if (a_record["FormID"] != nullptr)
		return LookupFormID<T>(a_record["FormID"]);
	if (a_record["EditorID"] != nullptr)
		return LookupEditorID<T>(a_record["EditorID"]);
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

void DataStorage::RunConfig(json& a_jsonData)
{
	for (auto& record : a_jsonData["Region"]) {
		if (auto regn = LookupForm<RE::TESRegion>(record)){
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
					if (rdsa["Sound"]; auto sound = LookupFormID<RE::BGSSoundDescriptorForm>(rdsa["Sound"])) {
						auto soundRecord = new RE::TESRegionDataSound::Sound;
						soundRecord->sound = sound;
						std::list<std::string> flagsList = rdsa["Flags"];
						soundRecord->flags = GetSoundFlags(flagsList);
						soundRecord->chance = (float)rdsa["Chance"];
						regionDataEntry->sounds.emplace_back(soundRecord);
					}
				}
			} else {
				logger::warn("RDSA entry does not exist in form {} {:X}", regn->GetFormEditorID(), regn->formID);
			}
		}
	}
	for (auto& record : a_jsonData["Weapon"]) {
		if (auto weap = LookupForm<RE::TESObjectWEAP>(record)) {
			if (record["Pick Up"]; auto ynam = LookupFormID<RE::BGSSoundDescriptorForm>(record["Pick Up"])) {
				weap->pickupSound = ynam;
			}

			if (record["Put Down"]; auto znam = LookupFormID<RE::BGSSoundDescriptorForm>(record["Put Down"])) {
				weap->putdownSound = znam;
			}

			if (record["Impact Data Set"]; auto inam = LookupEditorID<RE::BGSImpactDataSet>(record["Impact Data Set"]))
				weap->impactDataSet = inam;

			if (record["Attack"]; auto snam = LookupEditorID<RE::BGSSoundDescriptorForm>(record["Attack"]))
				weap->attackSound = snam;

			if (record["Attack 2D"]; auto xnam = LookupEditorID<RE::BGSSoundDescriptorForm>(record["Attack 2D"]))
				weap->attackSound2D = xnam;

			if (record["Attack Loop"]; auto nam7 = LookupEditorID<RE::BGSSoundDescriptorForm>(record["Attack Loop"]))
				weap->attackLoopSound = nam7;

			if (record["Attack Fail"]; auto tnam = LookupEditorID<RE::BGSSoundDescriptorForm>(record["Idle"]))
				weap->attackFailSound = tnam;

			if (record["Idle"]; auto unam = LookupEditorID<RE::BGSSoundDescriptorForm>(record["Unequp"]))
				weap->idleSound = unam;

			if (record["Equip"]; auto nam9 = LookupEditorID<RE::BGSSoundDescriptorForm>(record["Equip"]))
				weap->equipSound = nam9;

			if (record["Unequp"]; auto nam8 = LookupEditorID<RE::BGSSoundDescriptorForm>(record["Unequp"]))
				weap->unequipSound = nam8;
		}
	}

	for (auto& record : a_jsonData["Magic Effect"]) {
		if (auto mgef = LookupForm<RE::EffectSetting>(record)) {
			RE::BGSSoundDescriptorForm* slots[6];

			for (int i = 0; i < 6; i++) {
				auto soundID = std::string(magic_enum::enum_name((RE::MagicSystem::SoundID)i));
				soundID = soundID.substr(1, soundID.length() - 1);
				slots[i] = record[soundID] ? LookupFormID<RE::BGSSoundDescriptorForm>(record[soundID]) : nullptr;
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
					//auto soundPair = new RE::EffectSetting::SoundPair;
					//soundPair->id = (RE::MagicSystem::SoundID)i;
					//soundPair->sound = slots[i];
					//soundPair->pad04 = 0;
					//mgef->effectSounds.emplace_back(soundPair);
					auto soundID = std::string(magic_enum::enum_name((RE::MagicSystem::SoundID)i));
					soundID = soundID.substr(1, soundID.length() - 1);
					logger::warn("Could not replace {} in {} {:X}", soundID, mgef->GetFormEditorID(), mgef->formID);
				}
			}
		}
	}

	for (auto& record : a_jsonData["Armor Addon"]) {
		if (auto arma = LookupForm<RE::TESObjectARMA>(record)) {
			if (record["Footstep"]; auto sndd = LookupFormID<RE::BGSFootstepSet>(record["Footstep"])) {
				arma->footstepSet = sndd;
			}
		}
	}

	for (auto& record : a_jsonData["Armor"]) {
		if (auto armo = LookupForm<RE::TESObjectARMO>(record)) {
			if (record["Pick Up"]; auto ynam = LookupFormID<RE::BGSSoundDescriptorForm>(record["Pick Up"])) {
				armo->pickupSound = ynam;
			}
			if (record["Put Down"]; auto znam = LookupFormID<RE::BGSSoundDescriptorForm>(record["Put Down"])) {
				armo->putdownSound = znam;
			}
		}
	}

	for (auto& record : a_jsonData["Misc. Item"]) {
		if (auto misc = LookupForm<RE::TESObjectMISC>(record)) {
			if (record["Pick Up"]; auto ynam = LookupFormID<RE::BGSSoundDescriptorForm>(record["Pick Up"])) {
				misc->pickupSound = ynam;
			}
			if (record["Put Down"]; auto znam = LookupFormID<RE::BGSSoundDescriptorForm>(record["Put Down"])) {
				misc->putdownSound = znam;
			}
		}
	}

	for (auto& record : a_jsonData["Soul Gem"]) {
		if (auto slgm = LookupForm<RE::TESSoulGem>(record)) {
			if (record["Pick Up"]; auto ynam = LookupFormID<RE::BGSSoundDescriptorForm>(record["Pick Up"])) {
				slgm->pickupSound = ynam;
			}
			if (record["Put Down"]; auto znam = LookupFormID<RE::BGSSoundDescriptorForm>(record["Put Down"])) {
				slgm->putdownSound = znam;
			}
		}
	}
}
