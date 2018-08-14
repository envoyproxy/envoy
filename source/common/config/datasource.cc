#include "common/config/datasource.h"

#include "common/filesystem/filesystem_impl.h"

#include "fmt/format.h"

namespace Envoy {
namespace Config {
namespace DataSource {

std::string read(const envoy::api::v2::core::DataSource& source, bool allow_empty) {
  switch (source.specifier_case()) {
  case envoy::api::v2::core::DataSource::kFilename:
    return Filesystem::fileReadToEnd(source.filename());
  case envoy::api::v2::core::DataSource::kInlineBytes:
    return source.inline_bytes();
  case envoy::api::v2::core::DataSource::kInlineString:
    return source.inline_string();
  default:
    if (!allow_empty) {
      throw EnvoyException(
          fmt::format("Unexpected DataSource::specifier_case(): {}", source.specifier_case()));
    }
    return "";
  }
}

absl::optional<std::string> getPath(const envoy::api::v2::core::DataSource& source) {
  return source.specifier_case() == envoy::api::v2::core::DataSource::kFilename
             ? absl::make_optional(source.filename())
             : absl::nullopt;
}

} // namespace DataSource
} // namespace Config
} // namespace Envoy
