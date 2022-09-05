#pragma once

#include <vector>
#include <stack>
#include <span>
#include <filesystem>
#include <deque>
#include <unordered_set>

namespace modloader {

struct Dependency;

enum struct LoadPhase {
    Mods = 0,
    EarlyMods = 1,
    Libs = 2,
};

struct SharedObject {
    std::filesystem::path path;
    explicit SharedObject(std::filesystem::path path) : path(std::move(path)) {}

    [[nodiscard]] std::vector<modloader::Dependency> getToLoad(const std::filesystem::path& dependencyDir, LoadPhase phase) const;
};

struct Dependency {
    SharedObject object;
    std::vector<Dependency> dependencies;

    Dependency(SharedObject object, std::vector<Dependency> dependencies) : object(std::move(object)), dependencies(std::move(dependencies)) {}
};

std::vector<SharedObject> listToLoad(const std::filesystem::path& dependencyDir, LoadPhase phase);

/// List of failed mods to load
std::vector<SharedObject> loadMods(std::span<SharedObject const> const mods, const std::filesystem::path& dependencyDir, std::unordered_set<std::string>& skipLoad, LoadPhase phase);

std::deque<Dependency> topologicalSort(std::span<Dependency const> list);
}  // namespace modloader