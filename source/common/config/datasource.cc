#include "common/config/datasource.h"

#include "common/filesystem/filesystem_impl.h"

#include "fmt/format.h"

namespace Envoy {
namespace Config {

std::string DataSource::read(bool allow_empty) const {
  switch (source_.specifier_case()) {
  case envoy::api::v2::core::DataSource::kFilename:
    return Filesystem::fileReadToEnd(source_.filename());
  case envoy::api::v2::core::DataSource::kInlineBytes:
    return source_.inline_bytes();
  case envoy::api::v2::core::DataSource::kInlineString:
    return source_.inline_string();
  default:
    if (!allow_empty) {
      throw EnvoyException(
          fmt::format("Unexpected DataSource::specifier_case(): {}", source_.specifier_case()));
    }
    return "";
  }
}

std::string DataSource::getPath() const {
  return source_.specifier_case() == envoy::api::v2::core::DataSource::kFilename
             ? source_.filename()
             : "";
}

} // namespace Config
} // namespace Envoy
