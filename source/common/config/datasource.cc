#include "source/common/config/datasource.h"

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/config/utility.h"

#include "fmt/format.h"

namespace Envoy {
namespace Config {
namespace DataSource {

namespace {
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
} // namespace

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

DynamicData::DynamicData(Event::Dispatcher& main_dispatcher,
                         ThreadLocal::TypedSlotPtr<ThreadLocalData> slot,
                         Filesystem::WatcherPtr watcher)
    : dispatcher_(main_dispatcher), slot_(std::move(slot)), watcher_(std::move(watcher)) {}

DynamicData::~DynamicData() {
  if (!dispatcher_.isThreadSafe()) {
    dispatcher_.post([to_delete = std::move(slot_)] {});
  }
}

const std::string& DynamicData::data() const {
  const auto thread_local_data = slot_->get();
  return thread_local_data.has_value() ? *thread_local_data->data_ : EMPTY_STRING;
}

const std::string& DataSourceProvider::data() const {
  if (absl::holds_alternative<std::string>(data_)) {
    return absl::get<std::string>(data_);
  }
  return absl::get<DynamicData>(data_).data();
}

absl::StatusOr<DataSourceProviderPtr> DataSourceProvider::create(const ProtoDataSource& source,
                                                                 Event::Dispatcher& main_dispatcher,
                                                                 ThreadLocal::SlotAllocator& tls,
                                                                 Api::Api& api, bool allow_empty,
                                                                 uint64_t max_size) {
  auto initial_data_or_error = read(source, allow_empty, api, max_size);
  RETURN_IF_NOT_OK_REF(initial_data_or_error.status());

  // read() only validates the size of the file and does not check the size of inline data.
  // We check the size of inline data here.
  // TODO(wbpcode): consider moving this check to read() to avoid duplicate checks.
  if (max_size > 0 && initial_data_or_error.value().length() > max_size) {
    return absl::InvalidArgumentError(fmt::format("response body size is {} bytes; maximum is {}",
                                                  initial_data_or_error.value().length(),
                                                  max_size));
  }

  if (!source.has_watched_directory() ||
      source.specifier_case() != envoy::config::core::v3::DataSource::kFilename) {
    return std::unique_ptr<DataSourceProvider>(
        new DataSourceProvider(std::move(initial_data_or_error).value()));
  }

  auto slot = ThreadLocal::TypedSlot<DynamicData::ThreadLocalData>::makeUnique(tls);
  slot->set([initial_data = std::make_shared<std::string>(
                 std::move(initial_data_or_error.value()))](Event::Dispatcher&) {
    return std::make_shared<DynamicData::ThreadLocalData>(initial_data);
  });

  const auto& filename = source.filename();
  auto watcher = main_dispatcher.createFilesystemWatcher();
  // DynamicData will ensure that the watcher is destroyed before the slot is destroyed.
  // TODO(wbpcode): use Config::WatchedDirectory instead of directly creating a watcher
  // if the Config::WatchedDirectory is exception-free in the future.
  auto watcher_status = watcher->addWatch(
      absl::StrCat(source.watched_directory().path(), "/"), Filesystem::Watcher::Events::MovedTo,
      [slot_ptr = slot.get(), &api, filename, allow_empty, max_size](uint32_t) -> absl::Status {
        auto new_data_or_error = readFile(filename, api, allow_empty, max_size);
        if (!new_data_or_error.ok()) {
          // Log an error but don't fail the watch to avoid throwing EnvoyException at runtime.
          ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::config), error,
                              "Failed to read file: {}", new_data_or_error.status().message());
          return absl::OkStatus();
        }
        slot_ptr->runOnAllThreads(
            [new_data = std::make_shared<std::string>(std::move(new_data_or_error.value()))](
                OptRef<DynamicData::ThreadLocalData> obj) {
              if (obj.has_value()) {
                obj->data_ = new_data;
              }
            });
        return absl::OkStatus();
      });
  RETURN_IF_NOT_OK(watcher_status);

  return std::unique_ptr<DataSourceProvider>(
      new DataSourceProvider(DynamicData(main_dispatcher, std::move(slot), std::move(watcher))));
}

} // namespace DataSource
} // namespace Config
} // namespace Envoy
