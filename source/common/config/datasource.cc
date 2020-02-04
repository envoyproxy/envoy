#include "common/config/datasource.h"

#include "envoy/config/core/v3/base.pb.h"

#include "fmt/format.h"

namespace Envoy {
namespace Config {
namespace DataSource {

std::string read(const envoy::config::core::v3::DataSource& source, bool allow_empty,
                 Api::Api& api) {
  switch (source.specifier_case()) {
  case envoy::config::core::v3::DataSource::SpecifierCase::kFilename:
    return api.fileSystem().fileReadToEnd(source.filename());
  case envoy::config::core::v3::DataSource::SpecifierCase::kInlineBytes:
    return source.inline_bytes();
  case envoy::config::core::v3::DataSource::SpecifierCase::kInlineString:
    return source.inline_string();
  default:
    if (!allow_empty) {
      throw EnvoyException(
          fmt::format("Unexpected DataSource::specifier_case(): {}", source.specifier_case()));
    }
    return "";
  }
}

absl::optional<std::string> getPath(const envoy::config::core::v3::DataSource& source) {
  return source.specifier_case() == envoy::config::core::v3::DataSource::SpecifierCase::kFilename
             ? absl::make_optional(source.filename())
             : absl::nullopt;
}

} // namespace DataSource
} // namespace Config
} // namespace Envoy
