#include "common/config/datasource.h"

#include "envoy/api/v3alpha/core/base.pb.h"

#include "fmt/format.h"

namespace Envoy {
namespace Config {
namespace DataSource {

std::string read(const envoy::api::v3alpha::core::DataSource& source, bool allow_empty,
                 Api::Api& api) {
  switch (source.specifier_case()) {
  case envoy::api::v3alpha::core::DataSource::SpecifierCase::kFilename:
    return api.fileSystem().fileReadToEnd(source.filename());
  case envoy::api::v3alpha::core::DataSource::SpecifierCase::kInlineBytes:
    return source.inline_bytes();
  case envoy::api::v3alpha::core::DataSource::SpecifierCase::kInlineString:
    return source.inline_string();
  default:
    if (!allow_empty) {
      throw EnvoyException(
          fmt::format("Unexpected DataSource::specifier_case(): {}", source.specifier_case()));
    }
    return "";
  }
}

absl::optional<std::string> getPath(const envoy::api::v3alpha::core::DataSource& source) {
  return source.specifier_case() == envoy::api::v3alpha::core::DataSource::SpecifierCase::kFilename
             ? absl::make_optional(source.filename())
             : absl::nullopt;
}

} // namespace DataSource
} // namespace Config
} // namespace Envoy
