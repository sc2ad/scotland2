#pragma once
#include "_config.h"

#include <array>
#include <deque>
#include <filesystem>
#include <optional>
#include <span>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/core.h>
#include "constexpr-map.hpp"

namespace modloader {

struct Dependency;
struct SharedObject;

enum struct LoadPhase {
  None,
  Libs,
  EarlyMods,
  Mods,
};

inline auto format_as(LoadPhase p) {
  return fmt::underlying(p);
}

using namespace std::literals::string_view_literals;
constexpr static ConstexprMap loadPhaseMap(std::array<std::pair<LoadPhase, std::string_view>, 3>{
    { { LoadPhase::Libs, "libs"sv }, { LoadPhase::EarlyMods, "early_mods"sv }, { LoadPhase::Mods, "mods"sv } } });

using ModloaderString = char const*;

using MissingDependency = SharedObject;
using DependencyResult = std::variant<MissingDependency, Dependency>;

struct ModInfo;

using SetupFunc = void (*)(ModInfo& modInfo) noexcept;
using LoadFunc = void (*)() noexcept;
using UnloadFunc = void (*)() noexcept;

struct SharedObject {
  std::filesystem::path path;
  explicit SharedObject(std::filesystem::path path) : path(std::move(path)) {}
  // Copies are explicit ONLY. Should move otherwise.
  explicit SharedObject(SharedObject const&) = default;
  SharedObject& operator=(SharedObject const&) = delete;
  SharedObject(SharedObject&&) noexcept = default;
  SharedObject& operator=(SharedObject&&) noexcept = default;

  /// @brief Returns a collection of dependency results for all of the dependencies of a SharedObject, as read from
  /// the ELF. Requires that the SharedObject is readable.
  /// @param dependencyDir The top level directory to load from
  /// @param phase The phase to start the reverse search from
  /// @param loadedDependencies The dependecies that we have loaded already. The string_views must remain in lifetime
  /// for the duration of this call.
  /// @return The collection of dependency results that were attempted to be resolved
  [[nodiscard]] std::vector<DependencyResult> getToLoad(
      std::filesystem::path const& dependencyDir, LoadPhase phase,
      std::unordered_map<std::string_view, std::vector<DependencyResult>>& loadedDependencies) const;

  [[nodiscard]] inline std::vector<DependencyResult> getToLoad(std::filesystem::path const& dependencyDir,
                                                               LoadPhase phase) const {
    std::unordered_map<std::string_view, std::vector<DependencyResult>> loadedDependencies{};
    return getToLoad(dependencyDir, phase, loadedDependencies);
  }
};

struct Dependency {
  SharedObject object;
  std::vector<DependencyResult> dependencies;

  Dependency(Dependency&&) noexcept = default;
  Dependency& operator=(Dependency&&) noexcept = default;
  // Copies are explicit ONLY. Should move otherwise.
  explicit Dependency(Dependency const&) = default;
  Dependency& operator=(Dependency const&) = delete;

  Dependency(SharedObject object, std::vector<DependencyResult> dependencies)
      : object(std::move(object)), dependencies(std::move(dependencies)) {}
};

struct ModInfo {
  ModloaderString name{};     // nullable
  ModloaderString version{};  // nullable
  size_t versionLong{};

  ModInfo(ModloaderString name, ModloaderString version, size_t versionLong)
      : name(name), version(version), versionLong(versionLong) {}
  // TODO: Remove and force versionLong to be provided?
  ModInfo(ModloaderString name, ModloaderString version) : name(name), version(version), versionLong(1) {}
};

struct FailedMod {
  SharedObject object;
  std::string failure;
  std::vector<DependencyResult> dependencies;

  FailedMod(FailedMod&&) noexcept = default;
  FailedMod& operator=(FailedMod&&) noexcept = default;
  FailedMod(FailedMod const&) = delete;
  FailedMod& operator=(FailedMod const&) = delete;

  FailedMod(SharedObject object, std::string failure, std::vector<DependencyResult> dependencies)
      : object(std::move(object)), failure(std::move(failure)), dependencies(std::move(dependencies)) {}
};

struct LoadedMod {
  ModInfo modInfo;
  SharedObject object;
  LoadPhase phase;

  std::optional<SetupFunc> setupFn;
  std::optional<LoadFunc> loadFn;
  std::optional<UnloadFunc> unloadFn;

  void* handle;

  LoadedMod(LoadedMod&&) noexcept = default;
  LoadedMod& operator=(LoadedMod&&) noexcept = default;
  LoadedMod(LoadedMod const&) = delete;
  LoadedMod& operator=(LoadedMod const&) = delete;

  LoadedMod(ModInfo modInfo, SharedObject object, LoadPhase phase, std::optional<SetupFunc> setupFn,
            std::optional<LoadFunc> loadFn, std::optional<UnloadFunc> unloadFn, void* handle)
      : modInfo(std::move(modInfo)),
        object(std::move(object)),
        phase(phase),
        setupFn(setupFn),
        loadFn(loadFn),
        unloadFn(unloadFn),
        handle(handle) {}

  /// @brief Calls the setup function on the mod
  /// @return true if the call exists and was called, false otherwise
  inline bool init() noexcept {
    if (setupFn) {
      (*setupFn)(modInfo);
      return true;
    }
    return false;
  }
  /// @brief Calls the load function on the mod
  /// @return true if the call exists and was called, false otherwise
  inline bool load() noexcept {
    if (loadFn) {
      (*loadFn)();
      return true;
    }
    return false;
  }
  /// @brief Calls the unload function on the mod
  /// @return true if the call exists and was called, false otherwise
  inline bool unload() noexcept {
    if (unloadFn) {
      (*unloadFn)();
      return true;
    }
    return false;
  }
  /// @brief Closes the loaded mod by dlclosing it
  /// @return An optional holding the error message, or nullopt on success
  [[nodiscard]] std::optional<std::string> close() const noexcept;
};

std::deque<Dependency> MODLOADER_EXPORT topologicalSort(std::span<DependencyResult const> list);
std::deque<Dependency> MODLOADER_EXPORT topologicalSort(std::vector<Dependency>&& list);
}  // namespace modloader