#pragma once

#include <memory>

#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/init/manager.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/backoff_strategy.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/config/datasource.h"
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
template <class DataType>
using DataSourceProviderSharedPtr = std::shared_ptr<DataSourceProvider<DataType>>;

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

template <class DataType>
using DataTransform = std::function<absl::StatusOr<std::shared_ptr<DataType>>(absl::string_view)>;

struct ProviderOptions {
  // Use an empty string if no DataSource case is specified.
  bool allow_empty{false};
  // Limit of file to read, default 0 means no limit.
  uint64_t max_size{0};
  // Watch for file modifications.
  bool modify_watch{false};
  // Hash content before transforming, and skip transforming if hash is the same.
  bool hash_content{false};
};

// DynamicData registers the file watches and a thread local slot. This class
// must be created and deleted on the dispatcher thread.
template <class DataType> class DynamicData {
public:
  struct ThreadLocalData : public ThreadLocal::ThreadLocalObject {
    ThreadLocalData(std::shared_ptr<DataType> data) : data_(std::move(data)) {}
    std::shared_ptr<DataType> data_;
  };

  DynamicData(const ProtoDataSource& source, Event::Dispatcher& main_dispatcher,
              ThreadLocal::SlotAllocator& tls, Api::Api& api,
              DataTransform<DataType> data_transform_cb, const ProviderOptions& options,
              std::shared_ptr<DataType> initial_data, uint64_t initial_hash,
              absl::AnyInvocable<void()> cleanup, absl::Status& creation_status)
      : dispatcher_(main_dispatcher), api_(api), options_(options), filename_(source.filename()),
        data_transform_(data_transform_cb), hash_(initial_hash), cleanup_(std::move(cleanup)) {
    slot_ =
        ThreadLocal::TypedSlot<typename DynamicData<DataType>::ThreadLocalData>::makeUnique(tls);
    slot_->set([initial_data = std::move(initial_data)](Event::Dispatcher&) {
      return std::make_shared<typename DynamicData<DataType>::ThreadLocalData>(initial_data);
    });

    if (source.has_watched_directory()) {
      auto directory_watcher_or_error =
          WatchedDirectory::create(source.watched_directory(), main_dispatcher);
      SET_AND_RETURN_IF_NOT_OK(directory_watcher_or_error.status(), creation_status);
      watcher_ = *std::move(directory_watcher_or_error);
      watcher_->setCallback([this]() { return onWatchUpdate(); });
    }

    if (options.modify_watch) {
      modify_watcher_ = main_dispatcher.createFilesystemWatcher();
      SET_AND_RETURN_IF_NOT_OK(
          modify_watcher_->addWatch(filename_, Filesystem::Watcher::Events::Modified,
                                    [this](uint32_t) { return onWatchUpdate(); }),
          creation_status);
    }
  }

  ~DynamicData() {
    if (cleanup_) {
      cleanup_();
    }
  }

  std::shared_ptr<DataType> data() const {
    const auto thread_local_data = slot_->get();
    return thread_local_data.has_value() ? thread_local_data->data_ : nullptr;
  }

  Event::Dispatcher& dispatcher() { return dispatcher_; }

private:
  absl::Status onWatchUpdate() {
    auto new_data_or_error = readFile(filename_, api_, options_.allow_empty, options_.max_size);
    if (!new_data_or_error.ok()) {
      // Log an error but don't fail the watch to avoid throwing EnvoyException at runtime.
      ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::config), error,
                          "Failed to read file: {}", new_data_or_error.status().message());
      return absl::OkStatus();
    }
    uint64_t new_hash;
    if (options_.hash_content) {
      new_hash = HashUtil::xxHash64(*new_data_or_error);
      if (new_hash == hash_) {
        return absl::OkStatus();
      }
    }
    auto transformed_new_data_or_error = data_transform_(new_data_or_error.value());
    if (!transformed_new_data_or_error.ok()) {
      // Log an error but don't fail the watch to avoid throwing EnvoyException at runtime.
      ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::config), error,
                          "Failed to transform data from file '{}': {}", filename_,
                          transformed_new_data_or_error.status().message());
      return absl::OkStatus();
    }
    if (options_.hash_content) {
      hash_ = new_hash;
    }

    slot_->runOnAllThreads([new_data = std::move(transformed_new_data_or_error.value())](
                               OptRef<typename DynamicData<DataType>::ThreadLocalData> obj) {
      if (obj.has_value()) {
        obj->data_ = new_data;
      }
    });
    return absl::OkStatus();
  }

  Event::Dispatcher& dispatcher_;
  Api::Api& api_;
  const ProviderOptions options_;
  const std::string filename_;
  DataTransform<DataType> data_transform_;
  uint64_t hash_;
  absl::AnyInvocable<void()> cleanup_;
  ThreadLocal::TypedSlotPtr<ThreadLocalData> slot_;
  WatchedDirectoryPtr watcher_;
  Filesystem::WatcherPtr modify_watcher_;
};

/** Checks whether data source uses file watching. */
bool usesFileWatching(const ProtoDataSource& source, const ProviderOptions& options);

/**
 * DataSourceProvider provides a way to get the DataSource contents and watch the possible
 * content changes. The watch only works for filename-based DataSource and watched directory
 * is provided explicitly or with modify-watch enabled.
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
  static absl::StatusOr<DataSourceProviderPtr<DataType>>
  create(const ProtoDataSource& source, Event::Dispatcher& main_dispatcher,
         ThreadLocal::SlotAllocator& tls, Api::Api& api, bool allow_empty,
         DataTransform<DataType> data_transform_cb, uint64_t max_size) {
    return create(source, main_dispatcher, tls, api, data_transform_cb,
                  {.allow_empty = allow_empty, .max_size = max_size});
  }

  static absl::StatusOr<DataSourceProviderPtr<DataType>>
  create(const ProtoDataSource& source, Event::Dispatcher& main_dispatcher,
         ThreadLocal::SlotAllocator& tls, Api::Api& api, DataTransform<DataType> data_transform_cb,
         const ProviderOptions& options, absl::AnyInvocable<void()> cleanup = {}) {
    uint64_t max_size = options.max_size;
    auto initial_data_or_error = read(source, options.allow_empty, api, max_size);
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

    if (!usesFileWatching(source, options)) {
      return std::unique_ptr<DataSourceProvider<DataType>>(
          new DataSourceProvider<DataType>(std::move(*transformed_data_or_error.value())));
    }

    absl::Status creation_status = absl::OkStatus();
    const uint64_t hash = options.hash_content ? HashUtil::xxHash64(*initial_data_or_error) : 0;
    auto ret = std::unique_ptr<DataSourceProvider>(new DataSourceProvider<DataType>(
        std::make_unique<DynamicData<DataType>>(source, main_dispatcher, tls, api,
                                                data_transform_cb, options,
                                                std::move(transformed_data_or_error).value(), hash,
                                                std::move(cleanup), creation_status)));
    RETURN_IF_NOT_OK(creation_status);
    return std::move(ret);
  }

  std::shared_ptr<DataType> data() const {
    if (absl::holds_alternative<std::shared_ptr<DataType>>(data_)) {
      return absl::get<std::shared_ptr<DataType>>(data_);
    }
    return absl::get<std::unique_ptr<DynamicData<DataType>>>(data_)->data();
  }

  ~DataSourceProvider() {
    if (absl::holds_alternative<std::unique_ptr<DynamicData<DataType>>>(data_)) {
      // Schedule destruction on the dispatcher thread. This ensures that close()
      // stops any inotify events on the same thread.
      std::unique_ptr<DynamicData<DataType>> data =
          std::move(absl::get<std::unique_ptr<DynamicData<DataType>>>(data_));
      Event::Dispatcher& dispatcher = data->dispatcher();
      if (!dispatcher.isThreadSafe()) {
        dispatcher.post([to_delete = std::move(data)] {});
      }
    }
  }

private:
  DataSourceProvider(DataType&& data) : data_(std::make_shared<DataType>(std::move(data))) {}
  template <class... Args>
  DataSourceProvider(std::unique_ptr<DynamicData<DataType>> data) : data_(std::move(data)) {}

  absl::variant<std::shared_ptr<DataType>, std::unique_ptr<DynamicData<DataType>>> data_;
};

/**
 * ProviderSingleton allows sharing of dynamic DataSourceProviders using a process-wide
 * singleton instance. Only providers that rely on the file watching are shared, the rest
 * are created on-demand. This singleton reduces the resource pressure on the file watchers
 * and storage needed by the data type.
 */
template <class DataType>
class ProviderSingleton : public Singleton::Instance,
                          public std::enable_shared_from_this<ProviderSingleton<DataType>> {
public:
  ProviderSingleton(Event::Dispatcher& main_dispatcher, ThreadLocal::SlotAllocator& tls,
                    Api::Api& api, DataTransform<DataType> data_transform_cb,
                    const ProviderOptions& options)
      : dispatcher_(main_dispatcher), tls_(tls), api_(api), data_transform_(data_transform_cb),
        options_(options) {}

  absl::StatusOr<DataSourceProviderSharedPtr<DataType>> getOrCreate(const ProtoDataSource& source) {
    ASSERT_IS_MAIN_OR_TEST_THREAD();
    if (!usesFileWatching(source, options_)) {
      return DataSourceProvider<DataType>::create(source, dispatcher_, tls_, api_, data_transform_,
                                                  options_);
    }
    const size_t config_hash = MessageUtil::hash(source);
    auto it = dynamic_providers_.find(config_hash);
    if (it != dynamic_providers_.end()) {
      auto locked_provider = it->second.lock();
      if (locked_provider) {
        return locked_provider;
      }
    }

    // Cleanup is guaranteed to execute on the main dispatcher during destruction but may happen
    // after the singleton is released.
    auto provider_or_error = DataSourceProvider<DataType>::create(
        source, dispatcher_, tls_, api_, data_transform_, options_,
        [weak_this = this->weak_from_this(), config_hash] {
          if (auto locked_this = weak_this.lock(); locked_this) {
            locked_this->cleanup(config_hash);
          }
        });
    RETURN_IF_NOT_OK(provider_or_error.status());
    DataSourceProviderSharedPtr<DataType> new_provider = *std::move(provider_or_error);
    dynamic_providers_[config_hash] = new_provider;
    return new_provider;
  }

private:
  void cleanup(size_t key) {
    auto it = dynamic_providers_.find(key);
    if (it != dynamic_providers_.end() && it->second.expired()) {
      dynamic_providers_.erase(it);
    }
  }
  Event::Dispatcher& dispatcher_;
  ThreadLocal::SlotAllocator& tls_;
  Api::Api& api_;
  DataTransform<DataType> data_transform_;
  const ProviderOptions options_;
  absl::flat_hash_map<size_t, std::weak_ptr<DataSourceProvider<DataType>>> dynamic_providers_;
};

} // namespace DataSource
} // namespace Config
} // namespace Envoy
