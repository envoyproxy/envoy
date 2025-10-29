#include "source/common/config/datasource.h"

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/config/utility.h"

#include "fmt/format.h"

namespace Envoy {
namespace Config {
namespace DataSource {

/**
 * Read contents of the file.
 * @param path file path.
 * @param api reference to the Api.
 * @param allow_empty return an empty string if the file is empty.
 * @param max_size max size limit of file to read, default 0 means no limit, and if the file data
 * would exceed the limit, it will return an error status.
 * @return std::string with file contents. or an error status if the file does not exist or
 * cannot be read.
 */
absl::StatusOr<std::string> readFile(const std::string& path, Api::Api& api, bool allow_empty,
                                     uint64_t max_size) {
  auto& file_system = api.fileSystem();

  if (max_size > 0) {
    if (!file_system.fileExists(path)) {
      return absl::InvalidArgumentError(fmt::format("file {} does not exist", path));
    }
    const ssize_t size = file_system.fileSize(path);
    if (size < 0) {
      return absl::InvalidArgumentError(absl::StrCat("cannot determine size of file ", path));
    }
    if (static_cast<uint64_t>(size) > max_size) {
      return absl::InvalidArgumentError(
          fmt::format("file {} size is {} bytes; maximum is {}", path, size, max_size));
    }
  }

  auto file_content_or_error = file_system.fileReadToEnd(path);
  RETURN_IF_NOT_OK_REF(file_content_or_error.status());

  if (!allow_empty && file_content_or_error.value().empty()) {
    return absl::InvalidArgumentError(fmt::format("file {} is empty", path));
  }

  return file_content_or_error.value();
}

absl::StatusOr<std::string> read(const envoy::config::core::v3::DataSource& source,
                                 bool allow_empty, Api::Api& api, uint64_t max_size) {
  std::string data;
  switch (source.specifier_case()) {
  case envoy::config::core::v3::DataSource::SpecifierCase::kFilename:
    return readFile(source.filename(), api, allow_empty, max_size);
  case envoy::config::core::v3::DataSource::SpecifierCase::kInlineBytes:
    data = source.inline_bytes();
    break;
  case envoy::config::core::v3::DataSource::SpecifierCase::kInlineString:
    data = source.inline_string();
    break;
  case envoy::config::core::v3::DataSource::SpecifierCase::kEnvironmentVariable: {
    const char* environment_variable = std::getenv(source.environment_variable().c_str());
    if (environment_variable == nullptr) {
      return absl::InvalidArgumentError(
          fmt::format("Environment variable doesn't exist: {}", source.environment_variable()));
    }
    data = environment_variable;
    break;
  }
  default:
    if (!allow_empty) {
      return absl::InvalidArgumentError(fmt::format("Unexpected DataSource::specifier_case(): {}",
                                                    static_cast<int>(source.specifier_case())));
    }
  }
  if (!allow_empty && data.empty()) {
    return absl::InvalidArgumentError("DataSource cannot be empty");
  }
  return data;
}

absl::optional<std::string> getPath(const envoy::config::core::v3::DataSource& source) {
  return source.specifier_case() == envoy::config::core::v3::DataSource::SpecifierCase::kFilename
             ? absl::make_optional(source.filename())
             : absl::nullopt;
}

// Helper function for creating string-based DataSourceProvider with backward compatibility.
absl::StatusOr<DataSourceProviderPtr>
createStringDataSourceProvider(const ProtoDataSource& source, Event::Dispatcher& main_dispatcher,
                               ThreadLocal::SlotAllocator& tls, Api::Api& api, bool allow_empty,
                               uint64_t max_size) {
  // Identity transformation that wraps string in unique_ptr.
  auto identity_transform =
      [](const std::string& data) -> absl::StatusOr<std::unique_ptr<const std::string>> {
    return std::make_unique<const std::string>(data);
  };

  return DataSourceProvider<std::string>::create(source, main_dispatcher, tls, api, allow_empty,
                                                 max_size, identity_transform);
}

} // namespace DataSource
} // namespace Config
} // namespace Envoy
