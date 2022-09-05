#pragma once

#include <vector>
#include <stack>
#include <span>
#include <filesystem>
#include <deque>

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

    [[nodiscard]] std::vector<modloader::Dependency> getToLoad(std::filesystem::path dependencyDir, LoadPhase phase) const;
};

struct Dependency {
    SharedObject object;
    std::vector<Dependency> dependencies;

    Dependency(SharedObject object, std::vector<Dependency> dependencies) : object(std::move(object)), dependencies(std::move(dependencies)) {}
};

std::deque<Dependency> topologicalSort(std::span<Dependency const> list);
}  // namespace modloader