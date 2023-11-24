#include "runtime-restriction.hpp"
#include "elf-utils.hpp"
#include "log.h"
#include "linker_namespaces.hpp"

#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unordered_map>

#define PAGE_START(addr) (PAGE_MASK & addr)

namespace runtime_restriction {

  using namespace elf_utils;
    
  using get_soname_t = const char*(*)(soinfo* info);
  using get_primary_namespace_t = android_namespace_t*(*)(soinfo* info);

  std::unordered_map<uintptr_t, soinfo*>* g_soinfo_handles_map = nullptr;
  get_soname_t get_soname = nullptr;
  get_primary_namespace_t get_primary_namespace = nullptr;

  android_namespace_t* mainNamespace = nullptr;

  bool init(std::string_view modloaderFile) {
    if(!mainNamespace) {
      auto path = "/system/bin/linker64";
      int fd = open64(path, O_RDONLY | O_CLOEXEC);
      if (fd == -1) {
        LOG_ERROR("Failed to open dependency: {}: {}", path, std::strerror(errno));
        return false;
      }

      struct stat64 st {};
      if (fstat64(fd, &st) != 0) {
        LOG_ERROR("Failed to stat dependency: {}: {}", path, std::strerror(errno));
        return false;
      }
      size_t size = st.st_size;

      LOG_DEBUG("mmapping with size: {} on fd: {}", size, fd);
      // TODO: RAII-ify this
      void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);

      if (mapped == MAP_FAILED) {
        LOG_ERROR("Failed to mmap dependency: {}: {}", path, std::strerror(errno));
        return false;
      }

      std::span<uint8_t> f(static_cast<uint8_t*>(mapped), static_cast<uint8_t*>(mapped) + size);
      
      auto linkerBase = baseAddr("linker64");
      if(!linkerBase) {
        LOG_ERROR("Failed to get base address for linker64");
        return false;
      }

      g_soinfo_handles_map = reinterpret_cast<std::unordered_map<uintptr_t, soinfo*>*>(linkerBase + reinterpret_cast<uintptr_t>(getSymbol(f, "__dl_g_soinfo_handles_map")));
      if(reinterpret_cast<uintptr_t>(g_soinfo_handles_map) == linkerBase) {
        LOG_ERROR("Failed to get symbol g_soinfo_handles_map");
        return false;
      }
      LOG_DEBUG("g_soinfo_handles_map: {}", fmt::ptr(g_soinfo_handles_map));
      get_soname = reinterpret_cast<get_soname_t>(linkerBase + reinterpret_cast<uintptr_t>(getSymbol(f, "__dl__ZNK6soinfo10get_sonameEv")));
      if(reinterpret_cast<uintptr_t>(get_soname) == linkerBase) {
        LOG_ERROR("Failed to get symbol get_soname");
        return false;
      }
      LOG_DEBUG("get_soname: {}", fmt::ptr(get_soname));
      get_primary_namespace = reinterpret_cast<get_primary_namespace_t>(linkerBase + reinterpret_cast<uintptr_t>(getSymbol(f, "__dl__ZN6soinfo21get_primary_namespaceEv")));
      if(reinterpret_cast<uintptr_t>(get_primary_namespace) == linkerBase) {
        LOG_ERROR("Failed to get symbol get_primary_namespace");
        return false;
      }
      LOG_DEBUG("get_primary_namespace: {}", fmt::ptr(get_primary_namespace));

      for (auto&& [hdl, info] : *g_soinfo_handles_map) {
        if(std::string(get_soname(info)) == modloaderFile) { 
          mainNamespace = get_primary_namespace(info); 
          break;
        }
      }
      if(mainNamespace) {
        mprotect(reinterpret_cast<void*>(PAGE_START(reinterpret_cast<uintptr_t>(mainNamespace))), PAGE_SIZE, PROT_READ | PROT_WRITE);
        mainNamespace->set_isolated(false);
      } else {
          LOG_ERROR("Failed to get modloader namespace");
          return false;
      }
      LOG_DEBUG("modloader namespace: {} {}", mainNamespace->get_name(), fmt::ptr(mainNamespace));
    }
    return true;
  }

  bool add_ld_library_paths(std::vector<std::string>&& paths) {
    if(!mainNamespace)
      return false;
    std::vector<std::string> ldPaths = mainNamespace->get_ld_library_paths();
    ldPaths.insert(ldPaths.end(), paths.begin(), paths.end());
    mainNamespace->set_ld_library_paths(std::move(ldPaths));
    return true;
  }

}