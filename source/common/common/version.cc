#include "common/common/version.h"

#include <string>

#include "common/common/fmt.h"
#include "common/common/macros.h"
#include "common/common/version_linkstamp.h"

extern const char build_scm_revision[];
extern const char build_scm_status[];

namespace Envoy {
const std::string& VersionInfo::revision() {
  CONSTRUCT_ON_FIRST_USE(std::string, build_scm_revision);
}

const std::string& VersionInfo::revisionStatus() {
  CONSTRUCT_ON_FIRST_USE(std::string, build_scm_status);
}

const std::string& VersionInfo::version() {
  CONSTRUCT_ON_FIRST_USE(std::string, fmt::format("{}/{}/{}/{}/{}", revision(),
                                                  BUILD_VERSION_NUMBER, revisionStatus(),
#ifdef NDEBUG
                                                  "RELEASE",
#else
                                                  "DEBUG",
#endif
#ifdef ENVOY_SSL_VERSION
                                                  ENVOY_SSL_VERSION
#else
                                                  "no-ssl"
#endif
                                                  ));
}
} // namespace Envoy
