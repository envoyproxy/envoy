#pragma once

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "envoy/filesystem/filesystem.h"

#include "common/common/assert.h"

namespace Envoy {

static void check_sysctl_long(std::string path, long expected, Filesystem::Instance& file_system) {
  if (file_system.fileExists(path)) {
    long current = std::stol(file_system.fileReadToEnd(path));

    if (current < expected) {
      std::cerr << fmt::format("{} is set to {} please consider increasing it.", current, path)
                << std::endl;
    }
  }
}

// Check system wide configurations such as user level
// file watches, file limit permissions etc...
static void check_platform_settings(Filesystem::Instance& file_system) {

  struct rlimit current_limits;

  if (getrlimit(RLIMIT_NOFILE, &current_limits) == 0) {
    if (current_limits.rlim_max < 102400) {
      std::cerr << fmt::format("It looks like envoy can open only upto {} files, which is a low "
                               "value, consider increasing it using `ulimit`.",
                               current_limits.rlim_max)
                << std::endl;
    }
  }

  check_sysctl_long("/proc/sys/fs/inotify/max_user_watches", 500000, file_system);
}

} // namespace Envoy
