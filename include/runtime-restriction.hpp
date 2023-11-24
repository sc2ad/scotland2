#pragma once

#include "linker/linker_namespaces.hpp"

namespace runtime_restriction {

  void init();

  void add_ld_library_paths(std::vector<std::string>&& paths);

}