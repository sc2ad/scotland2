#include "loader.hpp"
#include "constexpr-map.hpp"
#include "internal-loader.hpp"
#include "log.h"

#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// NOTE: This is 64 bit specific!
// For 32 bit support, this file will need to support Elf32_Shdr*, etc.
namespace {
template <typename T>
T& readAtOffset(std::span<uint8_t> f, uint64_t offset) noexcept {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return *reinterpret_cast<T*>(&f[offset]);
}

std::string_view readAtOffset(std::span<uint8_t> const f, uint64_t offset) noexcept {
  return { readAtOffset<char const*>(f, offset) };
}

template <typename T>
std::span<T> readManyAtOffset(std::span<uint8_t> f, uint64_t offset, size_t amount, size_t size) noexcept {
  T* begin = &readAtOffset<T>(f, offset);
  T* end = begin + (amount * size);
  return std::span<T>(begin, end);
}

Elf64_Shdr& getSectionHeader(std::span<uint8_t> f, Elf64_Ehdr const& elf, uint16_t s) noexcept {
  return readAtOffset<Elf64_Shdr>(f, elf.e_shoff + static_cast<uint64_t>(elf.e_shentsize * s));
};

}  // namespace
namespace modloader {

/// @brief Find and create a SharedObject representing the resolved dependency, if it can be found.
/// @param dependencyDir The top level directory
/// @param phase The load phase of the dependency to start the search from
/// @param name The dependency's name to open
/// @return The returned pair on success, nullopt otherwise
std::optional<std::pair<SharedObject, LoadPhase>> findSharedObject(std::filesystem::path const& dependencyDir,
                                                                   LoadPhase phase, std::filesystem::path const& name) {
  // Search in reverse load order, starting at phase
  std::error_code error_code;
  for (auto const& it : loadPhaseMap.arr) {
    if (it.first != phase) {
      continue;
    }
    auto path_to_check = dependencyDir / it.second / name;
    if (std::filesystem::exists(path_to_check, error_code)) {
      // Dependency exists at this phase.
      return { std::make_pair(SharedObject(path_to_check), it.first) };
    }
    if (error_code) {
      LOG_ERROR("Failed to check for existence of: %s: %s", path_to_check.c_str(), error_code.message().c_str());
      return {};
    }
    // Otherwise, this filename doesn't exist under this phase. Try the next one.
  }
  // If we get to a point where we tried all of our explicit dependencies, return a None phase and try to let the
  // linker determine it when opening it.
  return { { SharedObject(name), LoadPhase::None } };
}

std::vector<DependencyResult> SharedObject::getToLoad(
    std::filesystem::path const& dependencyDir, LoadPhase phase,
    std::unordered_map<std::string_view, std::vector<DependencyResult>>& loadedDependencies) const {
  auto depIt = loadedDependencies.find(path.c_str());

  if (depIt != loadedDependencies.end()) {
    return depIt->second;
  }

  int fd = open64(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd == -1) {
    LOG_ERROR("Failed to open dependency: %s: %s", path.c_str(), std::strerror(errno));
    return {};
  }

  struct stat64 st {};
  if (fstat64(fd, &st) != 0) {
    LOG_ERROR("Failed to stat dependency: %s: %s", path.c_str(), std::strerror(errno));
    return {};
  }
  size_t size = st.st_size;

  void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);

  if (mapped == MAP_FAILED) {
    LOG_ERROR("Failed to mmap dependency: %s: %s", path.c_str(), std::strerror(errno));
  }

  std::span<uint8_t> f(static_cast<uint8_t*>(mapped), static_cast<uint8_t*>(mapped) + size);

  auto elf = readAtOffset<Elf64_Ehdr>(f, 0);

  std::vector<DependencyResult>& dependencies =
      loadedDependencies.emplace(path.c_str(), std::vector<DependencyResult>{}).first->second;
  // Micro-optimization: Small dependency sets will not alloc, larger ones will alloc less frequently
  dependencies.reserve(16);

  for (size_t s = 0; s < elf.e_shnum; s++) {
    auto const& sectionHeader = getSectionHeader(f, elf, s);
    if (sectionHeader.sh_type != SHT_DYNAMIC) {
      continue;
    }

    size_t num_deps = (sectionHeader.sh_entsize > 0) ? sectionHeader.sh_size / sectionHeader.sh_entsize : 0;

    for (size_t d = 0; d < num_deps; d++) {
      auto const& dyn = readAtOffset<Elf64_Dyn>(f, sectionHeader.sh_offset + (sectionHeader.sh_entsize * d));
      if (dyn.d_tag != DT_NEEDED) {
        LOG_DEBUG("Skipping non-required dynamic tag for section: %zu dependency: %zu", s, d);
        continue;
      }

      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
      std::string_view name =
          readAtOffset(f, getSectionHeader(f, elf, sectionHeader.sh_link).sh_offset + dyn.d_un.d_val);

      if (name.empty()) {
        LOG_WARN("SH undefined offset link name is empty/null, for dynamic section: %zu dependency: %zu of: %s", s, d,
                 path.c_str());
        continue;
      }

      auto optObj = findSharedObject(dependencyDir, phase, name);

      if (optObj) {
        auto [obj, openedPhase] = std::move(*optObj);
        if (openedPhase == LoadPhase::None) {
          // Unresolved dependency
          dependencies.emplace_back(std::in_place_type_t<MissingDependency>{}, std::move(obj));
        } else {
          // Resolved dependency
          // TODO: Make this avoid potentially stack overflowing on extremely nested dependency trees
          auto loadList = obj.getToLoad(dependencyDir, openedPhase, loadedDependencies);
          dependencies.emplace_back(std::in_place_type_t<Dependency>{}, std::move(obj), loadList);
        }
      } else {
        // Failed dependency (failed to check if it exists?)
        LOG_WARN("Skipping FAILED dependency: %s", name.data());
      }
    }
  }

  if (munmap(mapped, size) != 0) {
    LOG_ERROR("Failed to munmap %s: %s", path.c_str(), std::strerror(errno));
  }
  if (close(fd) != 0) {
    LOG_ERROR("Failed to close fd for: %s: %s", path.c_str(), std::strerror(errno));
  }

  return dependencies;
}

std::optional<std::string> LoadedMod::close() const noexcept {
  if (unloadFn) {
    (*unloadFn)();
  }
  if (dlclose(handle) != 0) {
    return dlerror();
  }
  return {};
}

void sortDependencies(std::span<Dependency> deps) {
  std::stable_sort(deps.begin(), deps.end(),
                   [](Dependency const& a, Dependency const& b) { return a.object.path > b.object.path; });
}

void sortDependencies(std::span<DependencyResult> deps) {
  std::stable_sort(deps.begin(), deps.end(), [](DependencyResult const& a, DependencyResult const& b) {
    std::string_view aPath;
    std::string_view bPath;
    if (holds_alternative<Dependency>(a)) {
      aPath = get<Dependency>(a).object.path.c_str();
    } else {
      aPath = get<MissingDependency>(a).path.c_str();
    }

    if (holds_alternative<Dependency>(b)) {
      bPath = get<Dependency>(b).object.path.c_str();
    } else {
      bPath = get<MissingDependency>(b).path.c_str();
    }

    return aPath > bPath;
  });
}

// https://www.geeksforgeeks.org/cpp-program-for-topological-sorting/
// Use mutable ref to avoid making a new vector that is sorted

/// @brief Performs a recursive topological sort for the given dependency, stack, and visited collection.
/// @param main The dependency to sort through. Must outlive the visited set
/// @param stack The stack to track the sorted dependencies
/// @param visited The set of all visited paths
void topologicalSortRecurse(Dependency& main, std::deque<Dependency>& stack,
                            std::unordered_set<std::string_view>& visited) {
  if (visited.contains(main.object.path.c_str())) {
    return;
  }

  visited.emplace(main.object.path.c_str());
  sortDependencies(main.dependencies);

  for (auto& depResult : main.dependencies) {
    auto* dep = get_if<Dependency>(&depResult);
    if (dep == nullptr) {
      continue;
    }
    topologicalSortRecurse(*dep, stack, visited);
  }

  stack.emplace_back(main);
}

// Copies FROM list into the dependencies
std::deque<Dependency> topologicalSort(std::span<DependencyResult const> list) {
  std::vector<Dependency> deps;
  deps.reserve(list.size());

  for (auto const& result : list) {
    auto const* dep = get_if<Dependency>(&result);
    if (dep == nullptr) {
      continue;
    }

    deps.push_back(*dep);
  }

  return topologicalSort(std::move(deps));
}

// Moves FROM list
std::deque<Dependency> topologicalSort(std::vector<Dependency>&& deps) {
  std::deque<Dependency> dependencies{};
  std::unordered_set<std::string_view> visited{};

  sortDependencies(deps);

  for (Dependency& dep : deps) {
    topologicalSortRecurse(dep, dependencies, visited);
  }

  return dependencies;
}

std::vector<SharedObject> listAllObjectsInPhase(std::filesystem::path const& dependencyDir, LoadPhase phase) {
  std::error_code error_code;
  // Note: We see through this iteration at compile time
  for (auto const& [ph, path] : loadPhaseMap.arr) {
    if (ph == phase) {
      std::vector<SharedObject> objects{};

      std::filesystem::directory_iterator dir_iter(dependencyDir / path, error_code);
      if (error_code) {
        LOG_ERROR("Failed to find objects in phase: %d from: %s: %s", phase, dependencyDir.c_str(),
                  error_code.message().c_str());
        return {};
      }
      for (auto const& file : dir_iter) {
        // All SharedObjects must be valid lib*.so files
        if (file.is_directory()) {
          continue;
        }
        if (file.path().extension() != ".so") {
          continue;
        }
        if (!file.path().filename().string().starts_with("lib")) {
          continue;
        }

        objects.emplace_back(file.path());
      }

      return objects;
    }
  }
  return {};
}

// handle or failure message
using OpenLibraryResult = std::variant<void*, std::string>;

OpenLibraryResult openLibrary(std::filesystem::path const& path) {
  auto* handle = dlopen(path.c_str(), RTLD_LOCAL | RTLD_NOW);
  if (handle == nullptr) {
    // Error logging (for if symbols cannot be resolved)
    return dlerror();
  }

  return { handle };
}

template <typename T>
std::optional<T> getFunction(void* handle, std::string_view name) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto ptr = reinterpret_cast<T>(dlsym(handle, name.data()));
  return ptr ? ptr : static_cast<std::optional<T>>(std::nullopt);
}

std::vector<LoadResult> loadMod(SharedObject&& mod, std::filesystem::path const& dependencyDir,
                                std::unordered_set<std::string>& skipLoad, LoadPhase phase) {
  if (skipLoad.contains(mod.path)) {
    LOG_WARN("Already loaded object at path: %s", mod.path.c_str());
    return {};
  }

  auto handleResult = [&](OpenLibraryResult&& result, SharedObject&& obj,
                          std::vector<DependencyResult>&& dependencies) -> LoadResult {
    if (auto const* error = get_if<std::string>(&result)) {
      return FailedMod(std::move(obj), *error, std::move(dependencies));
    }

    auto* handle = get<void*>(result);

    // Default modinfo is full path and v0.0.0, 0
    // The lifetime of the fullpath's c_str() is longer than this ModInfo, since this SharedObject will live forever
    ModInfo modInfo(obj.path.c_str(), "0.0.0", 0);

    auto setupFn = getFunction<SetupFunc>(handle, "setup");
    auto loadFn = getFunction<LoadFunc>(handle, "load");
    auto unloadFn = getFunction<UnloadFunc>(handle, "unload");

    return LoadedMod(modInfo, std::move(obj), phase, setupFn, loadFn, unloadFn, handle);
  };

  auto deps = mod.getToLoad(dependencyDir, phase);
  // Sorted is a COPY of deps
  auto sorted = topologicalSort(static_cast<std::span<DependencyResult const>>(deps));

  std::vector<LoadResult> results{};
  results.reserve(sorted.size());

  for (auto&& dep : sorted) {
    if (skipLoad.contains(dep.object.path)) {
      continue;
    }

    auto result = openLibrary(dep.object.path);
    skipLoad.emplace(dep.object.path);
    // The moves here are correct, because we are moving from the copied sorted collection
    auto const& handled =
        results.emplace_back(handleResult(std::move(result), std::move(dep.object), std::move(dep.dependencies)));

    if (auto const* failed = get_if<FailedMod>(&handled)) {
      // If we fail to open a dependency of the mod we are trying to open, we continue anyways, hoping that we will be
      // able to open later
      LOG_INFO("Failed to open dependency: %s for object: %s, %s! Trying anyways...", failed->object.path.c_str(),
               mod.path.c_str(), failed->failure.c_str());
    }
  }

  auto result = openLibrary(mod.path);
  skipLoad.emplace(mod.path);

  results.emplace_back(handleResult(std::move(result), std::move(mod), std::move(deps)));
  return results;
}

// mods is an OWNING span of SharedObjects! They will be moved FROM mods into results
std::vector<LoadResult> loadMods(std::span<SharedObject> mods, std::filesystem::path const& dependencyDir,
                                 std::unordered_set<std::string>& skipLoad, LoadPhase phase) {
  std::vector<LoadResult> results;
  results.reserve(mods.size());

  for (auto&& mod : mods) {
    if (skipLoad.contains(mod.path)) {
      continue;
    }

    auto otherResults = loadMod(std::move(mod), dependencyDir, skipLoad, phase);
    std::move(otherResults.begin(), otherResults.end(), std::back_inserter(results));
  }

  return results;
}
}  // namespace modloader
