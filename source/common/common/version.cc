#include "common/common/version.h"

#include <map>
#include <string>

#include "common/common/fmt.h"
#include "common/common/macros.h"
#include "common/common/version_linkstamp.h"
#include "common/protobuf/utility.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

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
  CONSTRUCT_ON_FIRST_USE(std::string,
                         fmt::format("{}/{}/{}/{}/{}", revision(), BUILD_VERSION_NUMBER,
                                     revisionStatus(), buildType(), sslVersion()));
}

const envoy::api::v2::core::BuildVersion& VersionInfo::buildVersion() {
  static envoy::api::v2::core::BuildVersion result = makeBuildVersion();
  return result;
}

const std::string& VersionInfo::buildType() {
#ifdef NDEBUG
  static const std::string release_type = "RELEASE";
#else
  static const std::string release_type = "DEBUG";
#endif
  return release_type;
}

const std::string& VersionInfo::sslVersion() {
#ifdef ENVOY_SSL_VERSION
  static const std::string ssl_version = ENVOY_SSL_VERSION;
#else
  static const std::string ssl_version = "no-ssl";
#endif
  return ssl_version;
}

envoy::api::v2::core::BuildVersion VersionInfo::makeBuildVersion() {
  envoy::api::v2::core::BuildVersion result;
  // Split BUILD_VERSION_NUMBER into version and a possible build label after the '-'
  std::vector<std::string> ver_label = absl::StrSplit(BUILD_VERSION_NUMBER, '-');
  std::vector<std::string> ver = absl::StrSplit(ver_label[0], '.');
  int value = 0;
  if (ver.size() > 0 && absl::SimpleAtoi(ver[0], &value)) {
    result.mutable_version()->set_major(value);
  }
  if (ver.size() > 1 && absl::SimpleAtoi(ver[1], &value)) {
    result.mutable_version()->set_minor(value);
  }
  if (ver.size() > 2 && absl::SimpleAtoi(ver[2], &value)) {
    result.mutable_version()->set_patch(value);
  }
  std::map<std::string, std::string> fields;
  if (ver_label.size() > 1) {
    fields[BuildVersionMetadataKeys::get().BuildLabel] = ver_label[1];
  }
  fields[BuildVersionMetadataKeys::get().BuildType] = buildType();
  fields[BuildVersionMetadataKeys::get().SslVersion] = sslVersion();
  fields[BuildVersionMetadataKeys::get().RevisionSHA] = revision();
  fields[BuildVersionMetadataKeys::get().RevisionStatus] = revisionStatus();
  *result.mutable_metadata() = MessageUtil::keyValueStruct(fields);
  return result;
}

} // namespace Envoy
