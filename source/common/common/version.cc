#include "common/common/version.h"

#include <spdlog/spdlog.h>

#include <string>

std::string VersionInfo::version() {
  return fmt::format("{}/{}", GIT_SHA.substr(0, 6),
#ifdef NDEBUG
                     "RELEASE"
#else
                     "DEBUG"
#endif
                     );
}
