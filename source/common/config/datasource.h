#pragma once

#include <functional>
#include <memory>

#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/init/manager.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/backoff_strategy.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/config/remote_data_fetcher.h"
#include "source/common/init/target_impl.h"

#include "absl/status/statusor.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Config {
namespace DataSource {

template <typename T> class DataSourceProvider;

using ProtoDataSource = envoy::config::core::v3::DataSource;
using ProtoWatchedDirectory = envoy::config::core::v3::WatchedDirectory;

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
                                     uint64_t max_size = 0);

/**
 * Read contents of the DataSource.
 * @param source data source.
 * @param allow_empty return an empty string if no DataSource case is specified.
 * @param api reference to the Api.
 * @param max_size max size limit of file to read, default 0 means no limit, and if the file data
 * would exceed the limit, it will return an error status.
 * @return std::string with DataSource contents. or an error status if no DataSource case is
 * specified and !allow_empty.
 */
absl::StatusOr<std::string> read(const envoy::config::core::v3::DataSource& source,
                                 bool allow_empty, Api::Api& api, uint64_t max_size = 0);

/**
 * @param source data source.
 * @return absl::optional<std::string> path to DataSource if a filename, otherwise absl::nullopt.
 */
absl::optional<std::string> getPath(const envoy::config::core::v3::DataSource& source);

/**
 * DynamicData manages thread-local storage for dynamically reloadable data of type T.
 * The data is watched and automatically updated when the underlying file changes.
 */
template <typename T> class DynamicData {
public:
  struct ThreadLocalData : public ThreadLocal::ThreadLocalObject {
    ThreadLocalData(std::shared_ptr<const T> data) : data_(std::move(data)) {}
    std::shared_ptr<const T> data_;
  };

  DynamicData(DynamicData&&) = default;
  DynamicData(Event::Dispatcher& main_dispatcher, ThreadLocal::TypedSlotPtr<ThreadLocalData> slot,
              Filesystem::WatcherPtr watcher);
  ~DynamicData();

  const T& data() const;

private:
  Event::Dispatcher& dispatcher_;
  ThreadLocal::TypedSlotPtr<ThreadLocalData> slot_;
  Filesystem::WatcherPtr watcher_;
};

/**
 * DataSourceProvider provides a way to get the DataSource contents and watch the possible
 * content changes. The watch only works for filename-based DataSource and watched directory
 * is provided explicitly.
 *
 * The provider is templated on the data type T and accepts a transformation callback that
 * converts raw string data into the target type. This callback is invoked both on initial
 * load and on file reload events.
 *
 * NOTE: This should only be used when the envoy.config.core.v3.DataSource is necessary and
 * file watch is required.
 */
template <typename T> class DataSourceProvider {
public:
  /**
   * Transformation callback type that converts raw string data to type T.
   * Returns unique_ptr<const T> for immutable data or error status on failure.
   */
  using TransformFn = std::function<absl::StatusOr<std::unique_ptr<const T>>(const std::string&)>;

  /**
   * Create a DataSourceProvider from a DataSource with data transformation.
   * @param source data source.
   * @param main_dispatcher reference to the main dispatcher.
   * @param tls reference to the thread local slot allocator.
   * @param api reference to the Api.
   * @param allow_empty return an empty string if no DataSource case is specified.
   * @param max_size max size limit of file to read, default 0 means no limit.
   * @param transform_fn callback to transform raw string data to type T.
   * @return absl::StatusOr<DataSourceProvider> with DataSource contents. or an error
   * status if any error occurs (including transformation failures).
   * NOTE: If file watch is enabled and the new file content does not meet the
   * requirements (allow_empty, max_size) or transformation fails, the provider will
   * keep the old content.
   */
  static absl::StatusOr<std::unique_ptr<DataSourceProvider<T>>>
  create(const ProtoDataSource& source, Event::Dispatcher& main_dispatcher,
         ThreadLocal::SlotAllocator& tls, Api::Api& api, bool allow_empty, uint64_t max_size,
         TransformFn transform_fn);

  const T& data() const;

private:
  DataSourceProvider(std::unique_ptr<const T> data) : data_(std::move(data)) {}
  DataSourceProvider(DynamicData<T>&& data) : data_(std::move(data)) {}

  absl::variant<std::unique_ptr<const T>, DynamicData<T>> data_;
};

/**
 * Type alias for string-based DataSourceProvider.
 */
using DataSourceProviderPtr = std::unique_ptr<DataSourceProvider<std::string>>;

/**
 * Helper function to create a string-based DataSourceProvider.
 * This uses an identity transformation that wraps the string in a unique_ptr.
 * @param source data source.
 * @param main_dispatcher reference to the main dispatcher.
 * @param tls reference to the thread local slot allocator.
 * @param api reference to the Api.
 * @param allow_empty return an empty string if no DataSource case is specified.
 * @param max_size max size limit of file to read, default 0 means no limit.
 * @return absl::StatusOr<DataSourceProvider<std::string>> with DataSource contents.
 */
absl::StatusOr<DataSourceProviderPtr>
createStringDataSourceProvider(const ProtoDataSource& source, Event::Dispatcher& main_dispatcher,
                               ThreadLocal::SlotAllocator& tls, Api::Api& api, bool allow_empty,
                               uint64_t max_size = 0);

// Template implementations.

template <typename T>
DynamicData<T>::DynamicData(Event::Dispatcher& main_dispatcher,
                            ThreadLocal::TypedSlotPtr<ThreadLocalData> slot,
                            Filesystem::WatcherPtr watcher)
    : dispatcher_(main_dispatcher), slot_(std::move(slot)), watcher_(std::move(watcher)) {}

template <typename T> DynamicData<T>::~DynamicData() {
  if (!dispatcher_.isThreadSafe()) {
    dispatcher_.post([to_delete = std::move(slot_)] {});
  }
}

template <typename T> const T& DynamicData<T>::data() const {
  const auto thread_local_data = slot_->get();
  ASSERT(thread_local_data.has_value() && thread_local_data->data_,
         "DynamicData accessed before initialization");
  return *thread_local_data->data_;
}

template <typename T> const T& DataSourceProvider<T>::data() const {
  if (absl::holds_alternative<std::unique_ptr<const T>>(data_)) {
    return *absl::get<std::unique_ptr<const T>>(data_);
  }
  return absl::get<DynamicData<T>>(data_).data();
}

template <typename T>
absl::StatusOr<std::unique_ptr<DataSourceProvider<T>>>
DataSourceProvider<T>::create(const ProtoDataSource& source, Event::Dispatcher& main_dispatcher,
                              ThreadLocal::SlotAllocator& tls, Api::Api& api, bool allow_empty,
                              uint64_t max_size, TransformFn transform_fn) {
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

  // Apply the transformation to the initial data.
  auto transformed_data_or_error = transform_fn(initial_data_or_error.value());
  RETURN_IF_NOT_OK(transformed_data_or_error.status());

  if (!source.has_watched_directory() ||
      source.specifier_case() != envoy::config::core::v3::DataSource::kFilename) {
    return std::unique_ptr<DataSourceProvider<T>>(
        new DataSourceProvider<T>(std::move(transformed_data_or_error).value()));
  }

  auto slot = ThreadLocal::TypedSlot<typename DynamicData<T>::ThreadLocalData>::makeUnique(tls);
  auto initial_data_shared = std::shared_ptr<const T>(std::move(transformed_data_or_error).value());
  slot->set([initial_data_shared](Event::Dispatcher&) {
    return std::make_shared<typename DynamicData<T>::ThreadLocalData>(initial_data_shared);
  });

  const auto& filename = source.filename();
  auto watcher = main_dispatcher.createFilesystemWatcher();
  // DynamicData will ensure that the watcher is destroyed before the slot is destroyed.
  // TODO(wbpcode): use Config::WatchedDirectory instead of directly creating a watcher
  // if the Config::WatchedDirectory is exception-free in the future.
  // Capture transform_fn by value to ensure it remains valid for the lifetime of the watcher.
  auto watcher_status = watcher->addWatch(
      absl::StrCat(source.watched_directory().path(), "/"), Filesystem::Watcher::Events::MovedTo,
      [slot_ptr = slot.get(), &api, filename, allow_empty, max_size,
       transform_fn](uint32_t) -> absl::Status {
        auto new_data_or_error = readFile(filename, api, allow_empty, max_size);
        if (!new_data_or_error.ok()) {
          // Log an error but don't fail the watch to avoid throwing EnvoyException at runtime.
          ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::config), error,
                              "Failed to read file: {}", new_data_or_error.status().message());
          return absl::OkStatus();
        }

        // Apply transformation to the new data.
        auto transformed_new_data_or_error = transform_fn(new_data_or_error.value());
        if (!transformed_new_data_or_error.ok()) {
          // Log an error but don't fail the watch; keep the old data.
          ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::config), error,
                              "Failed to transform reloaded data: {}",
                              transformed_new_data_or_error.status().message());
          return absl::OkStatus();
        }

        auto new_data_shared =
            std::shared_ptr<const T>(std::move(transformed_new_data_or_error).value());
        slot_ptr->runOnAllThreads(
            [new_data_shared](OptRef<typename DynamicData<T>::ThreadLocalData> obj) {
              if (obj.has_value()) {
                obj->data_ = new_data_shared;
              }
            });
        return absl::OkStatus();
      });
  RETURN_IF_NOT_OK(watcher_status);

  return std::unique_ptr<DataSourceProvider<T>>(new DataSourceProvider<T>(
      DynamicData<T>(main_dispatcher, std::move(slot), std::move(watcher))));
}

} // namespace DataSource
} // namespace Config
} // namespace Envoy
