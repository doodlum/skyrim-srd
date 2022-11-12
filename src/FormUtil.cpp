#include "FormUtil.h"

auto FormUtil::GetFormFromIdentifier(const std::string& a_identifier) -> RE::TESForm*
{
	std::istringstream ss{ a_identifier };
	std::string plugin, id;

	std::getline(ss, plugin, '|');
	std::getline(ss, id);
	RE::FormID relativeID;
	std::istringstream{ id } >> std::hex >> relativeID;
	const auto dataHandler = RE::TESDataHandler::GetSingleton();
	if (g_mergeMapperInterface) {
		const auto [mergedModName, mergedFormID] = g_mergeMapperInterface->GetNewFormID(plugin.c_str(), relativeID);
		std::string conversion_log = "";
		if (relativeID && mergedFormID && relativeID != mergedFormID) {
			conversion_log = std::format("0x{:x}->0x{:x}", relativeID, mergedFormID);
			relativeID = mergedFormID;
		}
		const std::string mergedModString{ mergedModName };
		if (!(plugin.empty()) && !mergedModString.empty() && plugin != mergedModString)
			{
			if (conversion_log.empty())
				conversion_log = std::format("{}->{}", plugin, mergedModString);
			else
				conversion_log = std::format("{}~{}->{}", conversion_log, plugin, mergedModString);
			plugin = mergedModName;
		}
		if (!conversion_log.empty())
			logger::debug("\t\tFound merged: {}", conversion_log);
	}
	return dataHandler ? dataHandler->LookupForm(relativeID, plugin) : nullptr;
}

auto FormUtil::GetIdentifierFromForm(const RE::TESForm* a_form) -> std::string
{
	auto editorID = a_form->GetFormEditorID();
	if (editorID && strlen(editorID) > 1) {
		return std::format("{}", editorID);
	}
	if (auto file = a_form->GetFile()) {
		return std::format("{:X}|{}", a_form->GetLocalFormID(), file->GetFilename());
	}
	return std::format("{:X}|Generated", a_form->GetLocalFormID());
}
