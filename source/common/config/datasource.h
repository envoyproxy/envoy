#pragma once

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
#include "source/common/config/watched_directory.h"
#include "source/common/init/target_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Config {
namespace DataSource {

template <class DataType> class DataSourceProvider;

using ProtoDataSource = envoy::config::core::v3::DataSource;
using ProtoWatchedDirectory = envoy::config::core::v3::WatchedDirectory;
template <class DataType>
using DataSourceProviderPtr = std::unique_ptr<DataSourceProvider<DataType>>;

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

template <class DataType> class DynamicData {
public:
  struct ThreadLocalData : public ThreadLocal::ThreadLocalObject {
    ThreadLocalData(std::shared_ptr<DataType> data) : data_(std::move(data)) {}
    std::shared_ptr<DataType> data_;
  };

  DynamicData(DynamicData&&) = default;
  DynamicData(Event::Dispatcher& main_dispatcher, ThreadLocal::TypedSlotPtr<ThreadLocalData> slot,
              WatchedDirectoryPtr watcher)
      : dispatcher_(main_dispatcher), slot_(std::move(slot)), watcher_(std::move(watcher)) {};
  ~DynamicData() {
    if (!dispatcher_.isThreadSafe()) {
      dispatcher_.post([to_delete = std::move(slot_)] {});
    }
  }

  const std::shared_ptr<DataType> data() const {
    const auto thread_local_data = slot_->get();
    return thread_local_data.has_value() ? thread_local_data->data_ : nullptr;
  }

private:
  Event::Dispatcher& dispatcher_;
  ThreadLocal::TypedSlotPtr<ThreadLocalData> slot_;
  WatchedDirectoryPtr watcher_;
};

/**
 * DataSourceProvider provides a way to get the DataSource contents and watch the possible
 * content changes. The watch only works for filename-based DataSource and watched directory
 * is provided explicitly.
 *
 * NOTE: This should only be used when the envoy.config.core.v3.DataSource is necessary and
 * file watch is required.
 */
template <class DataType> class DataSourceProvider {
public:
  /**
   * Create a DataSourceProvider from a DataSource.
   * @param source data source.
   * @param main_dispatcher reference to the main dispatcher.
   * @param tls reference to the thread local slot allocator.
   * @param api reference to the Api.
   * @param allow_empty return an empty string if no DataSource case is specified.
   * @param data_transform_cb transforms content of the DataSource (type std::string)
   *        to the desired `DataType` type.
   * @param max_size max size limit of file to read, default 0 means no limit.
   * @return absl::StatusOr<DataSourceProvider> with DataSource contents. or an error
   * status if any error occurs.
   * NOTE: If file watch is enabled and the new file content does not meet the
   * requirements (allow_empty, max_size), the provider will keep the old content.
   */
  static absl::StatusOr<DataSourceProviderPtr<DataType>> create(
      const ProtoDataSource& source, Event::Dispatcher& main_dispatcher,
      ThreadLocal::SlotAllocator& tls, Api::Api& api, bool allow_empty,
      std::function<absl::StatusOr<std::shared_ptr<DataType>>(absl::string_view)> data_transform_cb,
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
    auto transformed_data_or_error = data_transform_cb(initial_data_or_error.value());
    RETURN_IF_NOT_OK_REF(transformed_data_or_error.status());
    if (!source.has_watched_directory() ||
        source.specifier_case() != envoy::config::core::v3::DataSource::kFilename) {
      return std::unique_ptr<DataSourceProvider<DataType>>(
          new DataSourceProvider<DataType>(std::move(*transformed_data_or_error.value())));
    }

    auto slot =
        ThreadLocal::TypedSlot<typename DynamicData<DataType>::ThreadLocalData>::makeUnique(tls);

    slot->set([initial_data = std::make_shared<DataType>(
                   std::move(*transformed_data_or_error.value()))](Event::Dispatcher&) {
      return std::make_shared<typename DynamicData<DataType>::ThreadLocalData>(initial_data);
    });

    const auto& filename = source.filename();
    auto directory_watcher_or_error =
        WatchedDirectory::create(source.watched_directory(), main_dispatcher);
    RETURN_IF_NOT_OK_REF(directory_watcher_or_error.status());

    directory_watcher_or_error.value()->setCallback([slot_ptr = slot.get(), &api, filename,
                                                     allow_empty, max_size,
                                                     data_transform_cb]() -> absl::Status {
      auto new_data_or_error = readFile(filename, api, allow_empty, max_size);
      if (!new_data_or_error.ok()) {
        // Log an error but don't fail the watch to avoid throwing EnvoyException at runtime.
        ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::config), error,
                            "Failed to read file: {}", new_data_or_error.status().message());
        return absl::OkStatus();
      }
      auto transformed_new_data_or_error = data_transform_cb(new_data_or_error.value());
      if (!transformed_new_data_or_error.ok()) {
        // Log an error but don't fail the watch to avoid throwing EnvoyException at runtime.
        ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::config), error,
                            "Failed to transform data from file: {}",
                            transformed_new_data_or_error.status().message());
        return absl::OkStatus();
      }

      slot_ptr->runOnAllThreads([new_data = std::make_shared<DataType>(
                                     std::move(*transformed_new_data_or_error.value()))](
                                    OptRef<typename DynamicData<DataType>::ThreadLocalData> obj) {
        if (obj.has_value()) {
          obj->data_ = std::make_shared<DataType>(*new_data);
        }
      });
      return absl::OkStatus();
      ;
    });

    return std::unique_ptr<DataSourceProvider>(
        new DataSourceProvider<DataType>(DynamicData<DataType>(
            main_dispatcher, std::move(slot), std::move(directory_watcher_or_error.value()))));
  }

  const std::shared_ptr<DataType> data() const {
    if (absl::holds_alternative<std::shared_ptr<DataType>>(data_)) {
      return absl::get<std::shared_ptr<DataType>>(data_);
    }
    return absl::get<DynamicData<DataType>>(data_).data();
  }

  DataSourceProvider(DataType&& data) : data_(std::make_shared<DataType>(std::move(data))) {}
  DataSourceProvider(DynamicData<DataType>&& data) : data_(std::move(data)) {}

private:
  absl::variant<std::shared_ptr<DataType>, DynamicData<DataType>> data_;
};

} // namespace DataSource
} // namespace Config
} // namespace Envoy
