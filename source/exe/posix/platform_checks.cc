#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "envoy/common/exception.h"
#include "envoy/filesystem/filesystem.h"

#include "common/common/assert.h"

#include "exe/platform_checks.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

namespace Envoy {

void checkSysctlLong(const std::string& path, int64_t expected, Filesystem::Instance& file_system) {
  if (file_system.fileExists(path)) {
    int64_t current;
    try {
      if (absl::SimpleAtoi(file_system.fileReadToEnd(path), &current)) {
        if (current < expected) {
          ENVOY_LOG_MISC(error,
                         "{0} is set to {1} please consider increasing it using `sysctl`. Envoy "
                         "will log this error if the value is less than {2}",
                         path,    // 0
                         current, // 1
                         expected // 2
          );
        }

        return;
      }
    } catch (const EnvoyException& e) {
    }

    ENVOY_LOG_MISC(
        warn, "Could not read {}, please ensure that a reasonable value ({}) is set using `sysctl`",
        path, expected);
  }
}

// Check system wide configurations such as user level
// file watches, file limit permissions etc...
void checkPlatformSettings(Filesystem::Instance& file_system) {
  struct rlimit current_limits;

  int64_t min_open_files = 102400;
  int64_t min_inotify_watches = 500000;

  if (getrlimit(RLIMIT_NOFILE, &current_limits) == 0) {
    if (current_limits.rlim_max < 102400) {
      ENVOY_LOG_MISC(error,
                     "Hard limit for number of open files is {0}. Consider increasing it using "
                     "`ulimit`. Envoy will log this error if the value is less than {1}",
                     current_limits.rlim_max, // 0
                     min_open_files);         // 1
    }
  }

  checkSysctlLong("/proc/sys/fs/inotify/max_user_watches", min_inotify_watches, file_system);
}

} // namespace Envoy
