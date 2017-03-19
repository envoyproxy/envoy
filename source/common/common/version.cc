#include "common/common/version.h"

std::string VersionInfo::version() {
  return fmt::format("{}/{}", GIT_SHA.substr(0, 6),
#ifdef NDEBUG
                     "RELEASE"
#else
                     "DEBUG"
#endif
                     );
}
