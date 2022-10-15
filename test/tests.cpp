#include "tests.hpp"

#include "internal-loader.hpp"

#ifdef LINUX_TEST
#include <iostream>

template<typename T>
void writeSingle(auto& stream, T&& arg) {
    stream << std::forward<T>(arg);
}

template <typename... TArgs>
void write(TArgs&&... args) {
    (std::cout << ... << std::forward<TArgs>(args)) << std::endl;
}
#else
template <typename... TArgs>
void write(TArgs&&... args) {}
#endif

// recursion
void logDependencies(std::span<modloader::DependencyResult const> dependencies, size_t indent = 1) {
    for (auto const& result : dependencies) {
        std::string depName;

        const auto *dep = get_if<modloader::Dependency>(&result);
        if (dep != nullptr) {
            depName = dep->object.path.filename();
        } else {
            depName = "missing dependency " + std::string(get<modloader::MissingDependency>(result));
        }

        write(std::string(indent * 3, ' '), '-', depName);
        if (dep != nullptr) {
            logDependencies(dep->dependencies, indent + 1);
        }
    }
}

void tests::loadModsTest(const std::filesystem::path& path) {
    auto earlyMods = modloader::listModsInPhase(path, modloader::LoadPhase::EarlyMods);
    auto mods = modloader::listModsInPhase(path, modloader::LoadPhase::Mods);

    std::unordered_set<std::string> loadedPaths = {};

    write("Loading early mods");
    modloader::loadMods(earlyMods, path, loadedPaths, modloader::LoadPhase::EarlyMods);

    write("Test double load");
    modloader::loadMods(earlyMods, path, loadedPaths, modloader::LoadPhase::EarlyMods);

    write("Loading late mods");
    modloader::loadMods(mods, path, loadedPaths, modloader::LoadPhase::Mods);

}

std::vector<modloader::DependencyResult> tests::getDependencyTreeTest(const std::filesystem::path& dependencyPath, std::filesystem::path modPath) {
    auto mod = modloader::SharedObject(modPath);


    write("Loading ", mod.path.c_str());

    auto dependencies = mod.getToLoad(dependencyPath, modloader::LoadPhase::Mods);

    logDependencies(dependencies);

    write(std::string(3, '\n'), "Sorted:");

    return dependencies;
}

void tests::sortDependencyTreeTest(std::span<modloader::DependencyResult const> dependencies) {
    auto sorted = modloader::topologicalSort(dependencies);
    while (!sorted.empty()) {
        auto const& dep = sorted.front();
        write("-", dep.object.path.filename());
        sorted.pop_front();
    }
}