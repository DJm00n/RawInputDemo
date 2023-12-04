#pragma once

#pragma warning(push, 0)
#include <string>
#pragma warning(pop)

std::string GetLanguageNameWinRT(const std::string& languageTag);
std::string GetBcp47FromHklWinRT(HKL hkl);
