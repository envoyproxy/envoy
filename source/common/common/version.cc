#include "common/common/version.h"

#include <string>

#include "spdlog/spdlog.h"

extern const char build_scm_revision[];
extern const char build_scm_status[];

namespace Envoy {
const std::string& VersionInfo::revision() {
  static std::string* git_sha1 = new std::string(build_scm_revision);
  return *git_sha1;
}

const std::string& VersionInfo::revisionStatus() {
  static std::string* status = new std::string(build_scm_status);
  return *status;
}

std::string VersionInfo::version() {
  return fmt::format("{}/{}/{}", revision(), revisionStatus(),
#ifdef NDEBUG
                     "RELEASE"
#else
                     "DEBUG"
#endif
  );
}
} // namespace Envoy
