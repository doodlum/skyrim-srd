#include "DataStorage.h"

#include "FormUtil.h"
#include "tojson.hpp"

bool DataStorage::IsModLoaded(std::string_view a_modname)
{
	static const auto dataHandler = RE::TESDataHandler::GetSingleton();
	if (REL::Module::IsVR()) {
		auto& files = dataHandler->files;
		for (const auto file : files) {
			if (file->GetFilename() == a_modname && ((g_mergeMapperInterface) || file->GetCompileIndex() != 255))  // merged mods, will be index 255.
				return true;
		}
		return false;
	}
	return dataHandler->GetLoadedModIndex(a_modname) || dataHandler->GetLoadedLightModIndex(a_modname);
}

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
		if (entry.exists() && !entry.path().empty() && (entry.path().extension() == ".json"sv || entry.path().extension() == ".jsonc"sv || entry.path().extension() == ".yaml"sv)) {
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
		if (IsModLoaded(pluginname)) {
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
							filesString = filesString + " -> " + file;
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
					filesString = filesString + " -> " + file;
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
		auto path = std::filesystem::path(config).filename();
		auto filename = path.string();
		logger::info("Parsing {}", filename);
		currentFilename = filename;
		try {
			std::ifstream i(config);
			if (i.good()) {
				json data;
				if (path.extension() == ".yaml"sv) {
					try {
						logger::info("Converting {} to JSON object", filename);
						data = tojson::loadyaml(config);
					} catch (const std::exception& exc) {
						std::string errorMessage = std::format("Failed to convert {} to JSON object\n{}", filename, exc.what());
						logger::error("{}", errorMessage);
						RE::DebugMessageBox(errorMessage.c_str());
						continue;
					}
				} else {
					data = json::parse(i, nullptr, true, true);
				}
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
bool DataStorage::LookupFormString(T** a_type, json& a_record, std::string a_key, bool a_error)
{
	if (a_record.contains(a_key)) {
		if (!a_record[a_key].is_null()) {
			std::string formString = a_record[a_key];
			T* ret;
			if (formString.contains(".es") && formString.contains("|")) {
				ret = LookupFormID<T>(formString);
			} else {
				ret = LookupEditorID<T>(formString);
			}
			if (ret) {
				*a_type = ret;
				return true;
			} else {
				if (a_error) {
					std::string name = typeid(T).name();
					std::string errorMessage = std::format("	Form {} of {} does not exist in {}, this entry may be incomplete", formString, name, currentFilename);
					logger::error("{}", errorMessage);
					RE::DebugMessageBox(errorMessage.c_str());
				}
				return false;
			}
		} else {
			*a_type = nullptr;
			return true;
		}
	}
	return false;
}

template <typename T>
T* DataStorage::LookupForm(json& a_record)
{
	try {
		T* ret = nullptr;
		LookupFormString<T>(&ret, a_record, "Form", false); 
		if (!ret)
		{
			std::string identifier = a_record["Form"];
			std::string name = typeid(T).name();
			std::string errorMessage = std::format("	Form {} of {} does not exist in {}, skipping entry", identifier, name, currentFilename);
			logger::warn("{}", errorMessage);
		}
		return ret;
	} catch (const std::exception& exc) {
		std::string errorMessage = std::format("	Failed to parse entry in {}\n{}", currentFilename, exc.what());
		logger::error("{}", errorMessage);
		RE::DebugMessageBox(errorMessage.c_str());
	}
	return nullptr;
}

std::list<std::string> split(const std::string s, char delim)
{
	std::list<std::string> result;
	std::stringstream ss(s);
	std::string item;
	while (std::getline(ss, item, delim)) {
		result.push_back(item);
	}
	return result;
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
		for (auto& record : a_jsonData["Regions"]) {
			if (auto regn = LookupForm<RE::TESRegion>(record)) {
				RE::TESRegionDataSound* regionDataEntry = nullptr;
				for (auto entry : regn->dataList->regionDataList) {
					if (entry->GetType() == RE::TESRegionData::Type::kSound) {
						const auto regionDataManager = REL::Module::IsVR() ? reinterpret_cast<RE::TESRegionDataManager**>((char*)dataHandler + 0x1580) : &dataHandler->regionDataManager;
						if (regionDataManager)
							regionDataEntry = (*regionDataManager)->AsRegionDataSound(entry);
						if (regionDataEntry)
							break;
					}
				}
				if (regionDataEntry) {
					for (auto rdsa : record["RDSA"]) {
						RE::BGSSoundDescriptorForm* sound = nullptr;
						if (LookupFormString<RE::BGSSoundDescriptorForm>(&sound, rdsa, "Sound")) {
							bool created;
							std::list<std::string> changes;
							auto soundRecord = GetOrCreateSound(created, regionDataEntry->sounds, sound);
							soundRecord->sound = sound;

							if (rdsa.contains("Flags")) {
								soundRecord->flags = GetSoundFlags(split(rdsa["Flags"], ' '));
								changes.emplace_back("Flags");
							} else if (created) {
								soundRecord->flags = GetSoundFlags({ "Pleasant", "Cloudy", "Rainy", "Snowy" });
								changes.emplace_back("Flags");
							}
							if (rdsa.contains("Chance")) {
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
					std::string errorMessage = std::format("RDSA entry does not exist in {}", FormUtil::GetIdentifierFromForm(regn));
					logger::error("	{}", errorMessage);
					RE::DebugMessageBox(std::format("{}\n{}", currentFilename, errorMessage).c_str());
				}
			}
		}

		for (auto& record : a_jsonData["Weapons"]) {
			if (auto weap = LookupForm<RE::TESObjectWEAP>(record)) {
				std::list<std::string> changes;
				if (LookupFormString<RE::BGSSoundDescriptorForm>(&weap->pickupSound, record, "Pick Up"))
					changes.emplace_back("Pick Up");

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&weap->putdownSound, record, "Put Down"))
					changes.emplace_back("Put Down");

				if (LookupFormString<RE::BGSImpactDataSet>(&weap->impactDataSet, record, "Impact Data Set"))
					changes.emplace_back("Impact Data Set");

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&weap->attackSound, record, "Attack"))
					changes.emplace_back("Attack");

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&weap->attackSound2D, record, "Attack 2D"))
					changes.emplace_back("Attack 2D");

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&weap->attackLoopSound, record, "Attack Loop"))
					changes.emplace_back("Attack Loop");

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&weap->attackFailSound, record, "Attack Fail"))
					changes.emplace_back("Attack Fail");

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&weap->idleSound, record, "Idle"))
					changes.emplace_back("Idle");

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&weap->equipSound, record, "Equip"))
					changes.emplace_back("Equip");

				if (auto nam8 = LookupFormString<RE::BGSSoundDescriptorForm>(&weap->unequipSound, record, "Unequip"))
					changes.emplace_back("Unequip");

				InsertConflictInformation(weap, changes);
			}
		}

		for (auto& record : a_jsonData["Magic Effects"]) {
			if (auto mgef = LookupForm<RE::EffectSetting>(record)) {
				static const char* names[6] = {
					"Sheathe/Draw",
					"Charge",
					"Ready",
					"Release",
					"Cast Loop",
					"On Hit"
				};
				std::list<std::string> changes;
				RE::BGSSoundDescriptorForm* slots[6];
				bool useSlots[6] = { false, false, false, false, false, false };

				for (int i = 0; i < 6; i++) {
					auto soundID = names[i];
					useSlots[i] = LookupFormString<RE::BGSSoundDescriptorForm>(&slots[i], record, soundID);
					if (useSlots[i])
						changes.emplace_back(soundID);
				}

				for (auto sndd : mgef->effectSounds) {
					int i = (int)sndd.id;
					if (useSlots[i]) {
						sndd.sound = slots[i];
						sndd.pad04 = (bool)slots[i];
						useSlots[i] = false;
					}
				}

				for (int i = 0; i < 6; i++) {
					if (useSlots[i]) {
						RE::EffectSetting::SoundPair soundPair;
						soundPair.id = (RE::MagicSystem::SoundID)i;
						soundPair.sound = slots[i];
						soundPair.pad04 = (bool)slots[i];
						mgef->effectSounds.emplace_back(soundPair);
					}
				}
				InsertConflictInformation(mgef, changes);
			}
		}

		for (auto& record : a_jsonData["Armor Addons"]) {
			if (auto arma = LookupForm<RE::TESObjectARMA>(record)) {
				std::list<std::string> changes;

				if (LookupFormString<RE::BGSFootstepSet>(&arma->footstepSet, record, "Footstep"))
					changes.emplace_back("Footstep");

				InsertConflictInformation(arma, changes);
			}
		}

		for (auto& record : a_jsonData["Armors"]) {
			if (auto armo = LookupForm<RE::TESObjectARMO>(record)) {
				std::list<std::string> changes;

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&armo->pickupSound, record, "Pick Up"))
					changes.emplace_back("Pick Up");

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&armo->putdownSound, record, "Put Down"))
					changes.emplace_back("Put Down");

				InsertConflictInformation(armo, changes);
			}
		}

		for (auto& record : a_jsonData["Misc. Items"]) {
			if (auto misc = LookupForm<RE::TESObjectMISC>(record)) {
				std::list<std::string> changes;

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&misc->pickupSound, record, "Pick Up"))
					changes.emplace_back("Pick Up");

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&misc->putdownSound, record, "Put Down"))
					changes.emplace_back("Put Down");

				InsertConflictInformation(misc, changes);
			}
		}

		for (auto& record : a_jsonData["Soul Gems"]) {
			if (auto slgm = LookupForm<RE::TESSoulGem>(record)) {
				std::list<std::string> changes;

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&slgm->pickupSound, record, "Pick Up"))
					changes.emplace_back("Pick Up");

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&slgm->putdownSound, record, "Put Down"))
					changes.emplace_back("Put Down");

				InsertConflictInformation(slgm, changes);
			}
		}

		for (auto& record : a_jsonData["Projectiles"]) {
			if (auto proj = LookupForm<RE::BGSProjectile>(record)) {
				std::list<std::string> changes;

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&proj->data.activeSoundLoop, record, "Active"))
					changes.emplace_back("Active");

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&proj->data.countdownSound, record, "Countdown"))
					changes.emplace_back("Countdown");

				if (auto deactivateSound = LookupFormString<RE::BGSSoundDescriptorForm>(&proj->data.deactivateSound, record, "Deactivate"))
					changes.emplace_back("Deactivate");

				InsertConflictInformation(proj, changes);
			}
		}

		for (auto& record : a_jsonData["Explosions"]) {
			if (auto expl = LookupForm<RE::BGSExplosion>(record)) {
				std::list<std::string> changes;

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&expl->data.sound1, record, "Interior"))
					changes.emplace_back("Interior");

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&expl->data.sound1, record, "Exterior"))
					changes.emplace_back("Exterior");

				InsertConflictInformation(expl, changes);
			}
		}

		for (auto& record : a_jsonData["Effect Shaders"]) {
			if (auto efsh = LookupForm<RE::TESEffectShader>(record)) {
				std::list<std::string> changes;

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&efsh->data.ambientSound, record, "Ambient"))
					changes.emplace_back("Ambient");

				InsertConflictInformation(efsh, changes);
			}
		}
	}
}
