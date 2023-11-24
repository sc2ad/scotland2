#include "runtime-restriction.hpp"
#include "elf-utils.hpp"
#include "log.h"

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

  android_namespace_t* mainNamespace;


  void init() {
    if(!g_soinfo_handles_map) {
      auto path = "/system/bin/linker64";
      int fd = open64(path, O_RDONLY | O_CLOEXEC);
      if (fd == -1) {
        LOG_ERROR("Failed to open dependency: {}: {}", path, std::strerror(errno));
        return;
      }

      struct stat64 st {};
      if (fstat64(fd, &st) != 0) {
        LOG_ERROR("Failed to stat dependency: {}: {}", path, std::strerror(errno));
        return;
      }
      size_t size = st.st_size;

      LOG_DEBUG("mmapping with size: {} on fd: {}", size, fd);
      // TODO: RAII-ify this
      void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);

      if (mapped == MAP_FAILED) {
        LOG_ERROR("Failed to mmap dependency: {}: {}", path, std::strerror(errno));
      }

      std::span<uint8_t> f(static_cast<uint8_t*>(mapped), static_cast<uint8_t*>(mapped) + size);
      
      auto linkerBase = baseAddr("linker64");
      g_soinfo_handles_map = reinterpret_cast<std::unordered_map<uintptr_t, soinfo*>*>(linkerBase + reinterpret_cast<uintptr_t>(getSymbol(f, "__dl_g_soinfo_handles_map")));
      get_soname = reinterpret_cast<get_soname_t>(linkerBase + reinterpret_cast<uintptr_t>(getSymbol(f, "__dl__ZNK6soinfo10get_sonameEv")));
      get_primary_namespace =  reinterpret_cast<get_primary_namespace_t>(linkerBase + reinterpret_cast<uintptr_t>(getSymbol(f, "__dl__ZN6soinfo21get_primary_namespaceEv")));
      LOG_DEBUG("g_soinfo_handles_map: {}", reinterpret_cast<void*>(g_soinfo_handles_map));
      LOG_DEBUG("get_soname: {}", reinterpret_cast<void*>(get_soname));
      LOG_DEBUG("get_primary_namespace: {}", reinterpret_cast<void*>(get_primary_namespace));

      for (auto&& [hdl, info] : *g_soinfo_handles_map) {
        if(std::string(get_soname(info)) == "libmain.so") {
          mainNamespace = get_primary_namespace(info);
          mprotect(reinterpret_cast<void*>(PAGE_START(reinterpret_cast<uintptr_t>(mainNamespace))), PAGE_SIZE, PROT_READ | PROT_WRITE);
          break;
        }
      }
      LOG_DEBUG("libmain namespace: {} {}", mainNamespace->get_name(), reinterpret_cast<void*>(mainNamespace));
    }
  }

  void add_ld_library_paths(std::vector<std::string>&& paths) {
    init();
    std::vector<std::string> ldPaths = mainNamespace->get_ld_library_paths();
    ldPaths.insert(ldPaths.end(), paths.begin(), paths.end());
    mainNamespace->set_ld_library_paths(std::move(ldPaths));
    mainNamespace->set_isolated(false);
  }

}