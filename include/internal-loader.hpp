#pragma once

#include <vector>
#include <stack>
#include <span>
#include <filesystem>
#include <deque>
#include <unordered_set>
#include <variant>

#include "loader.hpp"

namespace modloader {

using LoadResult = std::variant<FailedMod, LoadedMod>;

std::vector<SharedObject> listModsInPhase(const std::filesystem::path& dependencyDir, LoadPhase phase);

/// List of failed mods to load
std::vector<LoadResult> loadMods(std::span<SharedObject const> mods, std::filesystem::path const& dependencyDir, std::unordered_set<std::string>& skipLoad, LoadPhase phase);
std::variant<FailedMod, LoadedMod> loadMod(SharedObject const& mod, std::filesystem::path const& dependencyDir, std::unordered_set<std::string>& skipLoad, LoadPhase phase);

}  // namespace modloader