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
#ifdef NDEBUG
  const std::string release_type = "RELEASE";
#else
  const std::string release_type = "DEBUG";
#endif
#ifdef ENVOY_SSL_VERSION
  const std::string ssl_version = ENVOY_SSL_VERSION;
#else
  const std::string ssl_version = "no-ssl";
#endif
  CONSTRUCT_ON_FIRST_USE(std::string,
                         fmt::format("{}/{}/{}/{}/{}", revision(), BUILD_VERSION_NUMBER,
                                     revisionStatus(), release_type, ssl_version));
}
} // namespace Envoy
