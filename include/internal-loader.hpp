#pragma once

#include <deque>
#include <filesystem>
#include <span>
#include <stack>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

#include "loader.hpp"

namespace modloader {

using LoadResult = std::variant<std::monostate, FailedMod, LoadedMod>;
static_assert(std::is_move_assignable_v<LoadResult> && std::is_move_constructible_v<LoadResult>, "");

std::vector<SharedObject> MODLOADER_EXPORT listAllObjectsInPhase(std::filesystem::path const& dependencyDir,
                                                                 LoadPhase phase);

/// List of failed mods to load
// Moves FROM mods
std::vector<LoadResult> MODLOADER_EXPORT loadMods(std::span<SharedObject> mods,
                                                  std::filesystem::path const& dependencyDir,
                                                  std::unordered_set<std::string>& skipLoad, LoadPhase phase);
std::vector<LoadResult> MODLOADER_EXPORT loadMod(SharedObject&& mod, std::filesystem::path const& dependencyDir,
                                                 std::unordered_set<std::string>& skipLoad, LoadPhase phase);

}  // namespace modloader
