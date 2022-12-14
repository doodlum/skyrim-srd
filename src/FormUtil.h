#pragma once

namespace FormUtil
{
	auto GetFormFromIdentifier(const std::string& a_identifier) -> RE::TESForm*;
	auto GetIdentifierFromForm(const RE::TESForm* a_form) -> std::string;
}
