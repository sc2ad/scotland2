#pragma once

#include <vector>
#include <stack>
#include <span>
#include <filesystem>
#include <deque>
#include <unordered_set>
#include <variant>

namespace modloader {

struct Dependency;

enum struct LoadPhase {
    Mods = 0,
    EarlyMods = 1,
    Libs = 2,
};

using MissingDependency = std::string;

using DependencyResult = std::variant<MissingDependency, Dependency>;

struct SharedObject {
    std::filesystem::path path;
    explicit SharedObject(std::filesystem::path path) : path(std::move(path)) {}

    [[nodiscard]] std::vector<DependencyResult> getToLoad(const std::filesystem::path& dependencyDir, LoadPhase phase) const;
};

struct Dependency {
    SharedObject object;
    std::vector<DependencyResult> dependencies;

    Dependency(SharedObject object, std::vector<DependencyResult> dependencies) : object(std::move(object)), dependencies(std::move(dependencies)) {}
};

std::vector<SharedObject> listToLoad(const std::filesystem::path& dependencyDir, LoadPhase phase);

/// List of failed mods to load
std::vector<SharedObject> loadMods(std::span<SharedObject const> const mods, const std::filesystem::path& dependencyDir, std::unordered_set<std::string>& skipLoad, LoadPhase phase);

std::deque<Dependency> topologicalSort(std::span<DependencyResult const> const list);
std::deque<Dependency> topologicalSort(std::span<Dependency const> list);
}  // namespace modloader