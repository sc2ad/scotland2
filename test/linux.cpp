#ifdef LINUX_TEST

#include <iostream>
#include "internal-loader.hpp"

void logDependencies(std::span<modloader::DependencyResult const> dependencies, size_t indent = 1) {
    for (auto const& result : dependencies) {
        std::string depName;

        const auto *dep = get_if<modloader::Dependency>(&result);
        if (dep != nullptr) {
            depName = dep->object.path.filename();
        } else {
            depName = "missing dependency " + get<modloader::MissingDependency>(result);
        }

        std::cout << std::string(indent * 3, ' ') << '-' << depName << std::endl;
        if (dep != nullptr) {
            logDependencies(dep->dependencies, indent + 1);
        }
    }
}

int main() {

    auto path = std::filesystem::current_path();
    auto mod = modloader::SharedObject(path/"test"/"libchroma.so");


    std::cout << "Loading " << mod.path.c_str() << std::endl;

    auto dependencies = mod.getToLoad(std::filesystem::current_path() / "test", modloader::LoadPhase::Mods);

    logDependencies(dependencies);

    std::cout << std::string(3, '\n') << "Sorted:" << std::endl;

    auto sorted = modloader::topologicalSort(dependencies);
    while (!sorted.empty()) {
        auto const& dep = sorted.front();
        std::cout << "-" << dep.object.path.filename() << std::endl;
        sorted.pop_front();
    }
}

#endif