#include "source/common/version/version_string.h"

#include "source/common/common/fmt.h"
#include "source/common/common/macros.h"
#include "source/common/version/version_number.h"

extern const char build_scm_revision[];
extern const char build_scm_status[];
extern const char build_version_suffix[];

namespace Envoy {

const std::string& envoyBuildType() {
#ifdef NDEBUG
  static const std::string type = "RELEASE";
#else
  static const std::string type = "DEBUG";
#endif
  return type;
}

const std::string& envoySSLVersion() {
#ifdef ENVOY_SSL_VERSION
  static const std::string version = ENVOY_SSL_VERSION;
#else
  static const std::string version = "no-ssl";
#endif
  return version;
}

const std::string& envoyVersionString() {
  CONSTRUCT_ON_FIRST_USE(std::string,
                         fmt::format("{}/{}{}/{}/{}/{}", build_scm_revision, BUILD_VERSION_NUMBER,
                                     build_version_suffix, build_scm_status, envoyBuildType(),
                                     envoySSLVersion()));
}

} // namespace Envoy
