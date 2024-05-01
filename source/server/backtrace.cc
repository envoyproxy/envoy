#include "source/server/backtrace.h"

#include <fstream>

#include "absl/strings/str_split.h"

namespace Envoy {

bool BackwardsTrace::log_to_stderr_ = false;

const std::string& BackwardsTrace::addrMapping(bool setup) {
  CONSTRUCT_ON_FIRST_USE(std::string, [setup]() -> std::string {
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
        return absl::StrCat(parts[0], " ", parts.back());
      }
    }
#endif
    return "";
  }());
}

void BackwardsTrace::setLogToStderr(bool log_to_stderr) { log_to_stderr_ = log_to_stderr; }

} // namespace Envoy
