#include "server/options_impl_platform_linux.h"

#include <sys/types.h>
#include <unistd.h>

#include <bitset>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "server/options_impl_platform.h"

namespace Envoy {

uint32_t OptionsImplPlatformLinux::getCpuCountFromPath(std::string& path, unsigned int hw_threads) {
  std::string key("Cpus_allowed:");
  std::ifstream file(path);
  if (!file) {
    // Fall back to number of hardware threads.
    return hw_threads;
  }

  std::string statusLine;
  unsigned int threads = 0;

  while (std::getline(file, statusLine)) {
    if (statusLine.compare(0, key.size(), key, 0, key.size()) == 0) {
      std::string value = statusLine.substr(key.size());
      for (std::string::iterator iter = value.begin(); iter != value.end(); iter++) {
        if (std::isxdigit(*iter)) {
          char buf[2];
          buf[0] = *iter;
          buf[1] = '\0';
          std::bitset<4> cpus(strtol(buf, NULL, 16));
          threads += cpus.count();
        }
      }
      break;
    }
  }
  // Sanity check.
  if (threads > 0 && threads <= hw_threads) {
    return threads;
  }
  return hw_threads;
}

uint32_t OptionsImplPlatform::getCpuCount() {
  std::string path("/proc/self/status");
  unsigned int hw_threads = std::max(1U, std::thread::hardware_concurrency());
  return OptionsImplPlatformLinux::getCpuCountFromPath(path, hw_threads);
}

} // namespace Envoy
