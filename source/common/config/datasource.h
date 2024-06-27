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
#include "source/common/init/target_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Config {
namespace DataSource {

class DataSourceProvider;

using ProtoDataSource = envoy::config::core::v3::DataSource;
using ProtoWatchedDirectory = envoy::config::core::v3::WatchedDirectory;
using DataSourceProviderPtr = std::unique_ptr<DataSourceProvider>;

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

class DynamicData {
public:
  struct ThreadLocalData : public ThreadLocal::ThreadLocalObject {
    ThreadLocalData(std::shared_ptr<std::string> data) : data_(std::move(data)) {}
    std::shared_ptr<std::string> data_;
  };

  DynamicData(DynamicData&&) = default;
  DynamicData(Event::Dispatcher& main_dispatcher, ThreadLocal::TypedSlotPtr<ThreadLocalData> slot,
              Filesystem::WatcherPtr watcher);
  ~DynamicData();

  const std::string& data() const;

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
 * NOTE: This should only be used when the envoy.config.core.v3.DataSource is necessary and
 * file watch is required.
 */
class DataSourceProvider {
public:
  /**
   * Create a DataSourceProvider from a DataSource.
   * @param source data source.
   * @param main_dispatcher reference to the main dispatcher.
   * @param tls reference to the thread local slot allocator.
   * @param api reference to the Api.
   * @param allow_empty return an empty string if no DataSource case is specified.
   * @param max_size max size limit of file to read, default 0 means no limit.
   * @return absl::StatusOr<DataSourceProvider> with DataSource contents. or an error
   * status if any error occurs.
   * NOTE: If file watch is enabled and the new file content does not meet the
   * requirements (allow_empty, max_size), the provider will keep the old content.
   */
  static absl::StatusOr<DataSourceProviderPtr>
  create(const ProtoDataSource& source, Event::Dispatcher& main_dispatcher,
         ThreadLocal::SlotAllocator& tls, Api::Api& api, bool allow_empty, uint64_t max_size = 0);

  const std::string& data() const;

private:
  DataSourceProvider(std::string&& data) : data_(std::move(data)) {}
  DataSourceProvider(DynamicData&& data) : data_(std::move(data)) {}

  absl::variant<std::string, DynamicData> data_;
};

} // namespace DataSource
} // namespace Config
} // namespace Envoy
