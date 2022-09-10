#pragma once

#include <utility>
#include <vector>
#include <stack>
#include <span>
#include <filesystem>
#include <deque>
#include <unordered_set>
#include <variant>
#include <optional>

namespace modloader {

struct Dependency;

enum struct LoadPhase {
    Mods = 0,
    EarlyMods = 1,
    Libs = 2,
};

using ModLoaderString = const char*;

using MissingDependency = ModLoaderString;
using DependencyResult = std::variant<MissingDependency, Dependency>;

struct ModInfo;

using SetupFunc = void(*)(ModInfo& modInfo);
using LoadFunc = void(*)();

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

struct ModInfo {
    ModLoaderString version{}; // nullable
    size_t versionLong{};
    ModLoaderString name{}; // nullable

    ModInfo(ModLoaderString version, size_t versionLong, ModLoaderString name) : version(version), versionLong(versionLong), name(name) {}
    // TODO: Remove and force versionLong to be provided?
    ModInfo(ModLoaderString version,  ModLoaderString name) : version(version), versionLong(1), name(name) {}
};

struct FailedMod {
    SharedObject object;
    ModLoaderString failure;
    std::vector<DependencyResult> dependencies;

    FailedMod(SharedObject object, ModLoaderString failure, std::vector<DependencyResult>  dependencies) : object(std::move(object)), failure(std::move(failure)), dependencies(std::move(dependencies)) {}
};

struct LoadedMod {
    ModInfo const modInfo;
    SharedObject const object;
    //    std::vector<MissingDependency> missingDependencies;

    std::optional<SetupFunc> const setupFn;
    std::optional<LoadFunc> const loadFn;

    void* const handle;

    LoadedMod(ModInfo modInfo, SharedObject object, std::optional<SetupFunc> setupFn, std::optional<LoadFunc> loadFn, void* handle)
        : modInfo(std::move(modInfo)), object(std::move(object)), setupFn(setupFn), loadFn(loadFn), handle(handle) {}
};

std::deque<Dependency> topologicalSort(std::span<DependencyResult const> list);
std::deque<Dependency> topologicalSort(std::span<Dependency const> list);
}  // namespace modloader