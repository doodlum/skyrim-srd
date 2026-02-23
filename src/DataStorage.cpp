#include "DataStorage.h"

#include "FormUtil.h"
#include "tojson.hpp"


bool DataStorage::IsModLoaded(std::string_view a_modname)
{
	static const auto dataHandler = RE::TESDataHandler::GetSingleton();
	constexpr std::uint8_t NOT_LOADED_IDX = 0xFF;

	for (const auto file : dataHandler->files) {
		if (!file) {
			continue;
		}

		if (file->GetFilename() != a_modname) {
			continue;
		}

		if (g_mergeMapperInterface) {
			return true;
		}

		const auto idx = file->GetCompileIndex();
		return idx != NOT_LOADED_IDX;
	}

	return false;
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

std::pair<std::set<std::string>, std::set<std::string>>
DataStorage::ScanConfigDirectory()
{
	std::set<std::string> generalConfigs;
	std::set<std::string> pluginConfigs;

	constexpr auto folder = R"(Data\)"sv;

	logger::info("\nScanning {} for configs ending with _SRD.json/.jsonc/.yaml...", folder);

	for (const auto& entry : std::filesystem::directory_iterator(folder)) {
		if (!entry.exists() || entry.path().empty()) {
			continue;
		}

		const auto ext = entry.path().extension().string();
		if (ext != ".json" && ext != ".jsonc" && ext != ".yaml") {
			continue;
		}

		const auto stem = entry.path().stem().string(); // filename without extension
		if (!stem.ends_with("_SRD")) {
			continue;
		}

		const auto path = entry.path().string();

		// Old logic: plugin configs contain ".es"
		if (stem.contains(".es")) {
			logger::info("Found plugin-specific config: {}", path);
			pluginConfigs.insert(path);
		} else {
			logger::info("Found general config: {}", path);
			generalConfigs.insert(path);
		}
	}

	return { generalConfigs, pluginConfigs };
}

std::map<std::string, std::set<std::string>>
DataStorage::MatchPluginConfigs(const std::set<std::string>& pluginConfigs)
{
	std::map<std::string, std::set<std::string>> result;

	auto* dataHandler = RE::TESDataHandler::GetSingleton();

	logger::info("Matching plugin-specific configs to loaded plugins...");
	for (auto* file : dataHandler->files) {
		if (!file) {
			continue;
		}

		const auto pluginName = file->GetFilename();

		if (!IsModLoaded(pluginName)) {
			logger::warn("Plugin {} is not loaded, skipping configs", pluginName);
			continue;
		}

		std::set<std::string> matched;

		for (const auto& configPath : pluginConfigs) {
			const auto configName = std::filesystem::path(configPath).filename().string();

			// Old logic: config filename starts with plugin name
			if (configName.rfind(pluginName, 0) == 0) {
				logger::info("Adding config {} for plugin {}", configName, pluginName);
				matched.insert(configPath);
			}
		}

		if (!matched.empty()) {
			result.emplace(pluginName, std::move(matched));
		}
	}

	return result;
}

void DataStorage::ParseAllConfigs(
	const std::map<std::string, std::set<std::string>>& pluginMap,
	const std::set<std::string>& generalConfigs)
{
	logger::info("\nParsing configs...");

	for (const auto& [plugin, configs] : pluginMap) {
		logger::info("Parsing {} configs for plugin {}", configs.size(), plugin);
		ParseConfigs(configs);
	}

	if (!generalConfigs.empty()) {
		logger::info("Parsing {} general configs", generalConfigs.size());
		ParseConfigs(generalConfigs);
	}
}

void DataStorage::PrintConflicts()
{
	logger::info("\nConflict summary:\n");
	if(conflictMapRegions.empty() && conflictMap.empty()) {
		logger::info("No conflicts found.");
		return;
	}

	for (auto& [region, soundMap] : conflictMapRegions) {
		if (soundMap.empty()) {
			continue;
		}

		logger::info("\n{}", FormUtil::GetIdentifierFromForm(region));

		for (auto& [sound, conflictInfo] : soundMap) {
			if (conflictInfo.empty()) {
				continue;
			}

			logger::info("    {}", FormUtil::GetIdentifierFromForm(sound));

			for (auto& [field, files] : conflictInfo) {
				std::string filesString;
				for (auto& file : files) {
					filesString += " -> " + file;
				}
				logger::info("        {} {}", field, filesString);
			}
		}
	}

	for (auto& [form, conflictInfo] : conflictMap) {
		if (conflictInfo.empty()) {
			continue;
		}

		logger::info("\n{}", FormUtil::GetIdentifierFromForm(form));

		for (auto& [field, files] : conflictInfo) {
			std::string filesString;
			for (auto& file : files) {
				filesString += " -> " + file;
			}
			logger::info("    {} {}", field, filesString);
		}
	}
}

void DataStorage::LoadConfigs()
{
	using clock = std::chrono::steady_clock;

	auto begin = clock::now();
	auto [generalConfigs, pluginConfigs] = ScanConfigDirectory();
	auto end = clock::now();

	logger::info("Scanned configs in {} ms\n",
				 std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());

	if (generalConfigs.empty() && pluginConfigs.empty()) {
		logger::warn("No configs found in Data\\ ending with _SRD.json/.jsonc/.yaml");
		return;
	}

	begin = clock::now();
	auto pluginMap = MatchPluginConfigs(pluginConfigs);
	ParseAllConfigs(pluginMap, generalConfigs);
	end = clock::now();

	logger::info("Parsed configs in {} ms",
				 std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());

	begin = clock::now();
	PrintConflicts();
	end = clock::now();

	logger::info("Printed conflicts in {} ms",
				 std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());
}

void DataStorage::ParseConfigs(const std::set<std::string>& a_configs)
{
	for (const auto& configPath : a_configs) {

		const std::filesystem::path path(configPath);
		const std::string filename = path.filename().string();
		const std::string extension = path.extension().string();

		logger::info("Parsing {}", filename);
		currentFilename = filename;

		try {
			std::ifstream file(configPath);

			if (!file.good()) {
				const std::string errorMessage =
				std::format("Failed to parse {}\nBad file stream", filename);
				logger::error("{}", errorMessage);
				RE::DebugMessageBox(errorMessage.c_str());
				continue;
			}

			json data;

			// YAML → JSON conversion
			if (extension == ".yaml") {
				try {
					logger::info("Converting {} to JSON object", filename);
					data = tojson::loadyaml(configPath);
				} catch (const std::exception& exc) {
					const std::string errorMessage =
					std::format("Failed to convert {} to JSON object\n{}", filename, exc.what());
					logger::error("{}", errorMessage);
					RE::DebugMessageBox(errorMessage.c_str());
					continue;
				}
			}
			// JSON / JSONC
			else {
				try {
					data = json::parse(file, nullptr, true, true);
				} catch (const std::exception& exc) {
					const std::string errorMessage =
					std::format("Failed to parse {}\n{}", filename, exc.what());
					logger::error("{}", errorMessage);
					RE::DebugMessageBox(errorMessage.c_str());
					continue;
				}
			}
			// Process the parsed config
			RunConfig(data);
		}
		catch (const std::exception& exc) {
			const std::string errorMessage =
			std::format("Failed to parse {}\n{}", filename, exc.what());
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
		if (!ret) {
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
						const auto regionDataManager = dataHandler->GetRegionDataManager();
						if (regionDataManager)
							regionDataEntry = regionDataManager->AsRegionDataSound(entry);
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

		for (auto& record : a_jsonData["Ingestibles"]) {
			if (auto efsh = LookupForm<RE::AlchemyItem>(record)) {
				std::list<std::string> changes;

				if (LookupFormString<RE::BGSSoundDescriptorForm>(&efsh->data.consumptionSound, record, "Consume"))
					changes.emplace_back("Consume");

				InsertConflictInformation(efsh, changes);
			}
		}
	}
}
