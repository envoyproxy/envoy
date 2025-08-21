#include "source/server/backtrace.h"

#include <fstream>

#include "absl/strings/str_split.h"

namespace Envoy {

bool BackwardsTrace::log_to_stderr_ = false;

absl::string_view BackwardsTrace::addrMapping(bool setup) {
  static absl::string_view value = [setup]() -> absl::string_view {
    if (!setup) {
      return "";
    }
#ifndef WIN32
    std::ifstream maps("/proc/self/maps");
    if (maps.fail()) {
      return "";
    }
    std::string line;
    // Search for the first executable memory mapped block.
    while (std::getline(maps, line)) {
      std::vector<absl::string_view> parts = absl::StrSplit(line, ' ');
      if (parts[1] == "r-xp") {
        static std::string result = absl::StrCat(parts[0], " ", parts.back());
        return result;
      }
    }
#endif
    return "";
  }();
  return value;
}

void BackwardsTrace::setLogToStderr(bool log_to_stderr) { log_to_stderr_ = log_to_stderr; }

} // namespace Envoy
