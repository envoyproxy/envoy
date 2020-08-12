#pragma once

#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/init/manager.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/backoff_strategy.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/config/remote_data_fetcher.h"
#include "common/init/target_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Config {
namespace DataSource {

/**
 * Read contents of the DataSource.
 * @param source data source.
 * @param allow_empty return an empty string if no DataSource case is specified.
 * @param api reference to the Api object
 * @return std::string with DataSource contents.
 * @throw EnvoyException if no DataSource case is specified and !allow_empty.
 */
std::string read(const envoy::config::core::v3::DataSource& source, bool allow_empty,
                 Api::Api& api);

/**
 * @param source data source.
 * @return absl::optional<std::string> path to DataSource if a filename, otherwise absl::nullopt.
 */
absl::optional<std::string> getPath(const envoy::config::core::v3::DataSource& source);

/**
 * Callback for async data source.
 */
using AsyncDataSourceCb = std::function<void(const std::string&)>;

class LocalAsyncDataProvider {
public:
  LocalAsyncDataProvider(Init::Manager& manager, const envoy::config::core::v3::DataSource& source,
                         bool allow_empty, Api::Api& api, AsyncDataSourceCb&& callback)
      : init_target_("LocalAsyncDataProvider", [this, &source, allow_empty, &api, callback]() {
          callback(DataSource::read(source, allow_empty, api));
          init_target_.ready();
        }) {
    manager.add(init_target_);
  }

  ~LocalAsyncDataProvider() { init_target_.ready(); }

private:
  Init::TargetImpl init_target_;
};

using LocalAsyncDataProviderPtr = std::unique_ptr<LocalAsyncDataProvider>;

class RemoteAsyncDataProvider : public Config::DataFetcher::RemoteDataFetcherCallback,
                                public Logger::Loggable<Logger::Id::config> {
public:
  RemoteAsyncDataProvider(Upstream::ClusterManager& cm, Init::Manager& manager,
                          const envoy::config::core::v3::RemoteDataSource& source,
                          Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
                          bool allow_empty, AsyncDataSourceCb&& callback);

  ~RemoteAsyncDataProvider() override {
    init_target_.ready();
    if (retry_timer_) {
      retry_timer_->disableTimer();
    }
  }

  // Config::DataFetcher::RemoteDataFetcherCallback
  void onSuccess(const std::string& data) override {
    callback_(data);
    init_target_.ready();
  }

  // Config::DataFetcher::RemoteDataFetcherCallback
  void onFailure(Config::DataFetcher::FailureReason failure) override {
    ENVOY_LOG(debug, "Failed to fetch remote data, failure reason: {}", enumToInt(failure));
    if (retries_remaining_-- == 0) {
      ENVOY_LOG(warn, "Retry limit exceeded for fetching data from remote data source.");
      if (allow_empty_) {
        callback_(EMPTY_STRING);
      }
      // We need to allow server startup to continue.
      init_target_.ready();
      return;
    }

    const auto retry_ms = std::chrono::milliseconds(backoff_strategy_->nextBackOffMs());
    ENVOY_LOG(debug, "Remote data provider will retry in {} ms.", retry_ms.count());
    retry_timer_->enableTimer(retry_ms);
  }

private:
  void start() { fetcher_->fetch(); }

  bool allow_empty_;
  AsyncDataSourceCb callback_;
  const Config::DataFetcher::RemoteDataFetcherPtr fetcher_;
  Init::TargetImpl init_target_;

  Event::TimerPtr retry_timer_;
  BackOffStrategyPtr backoff_strategy_;
  uint32_t retries_remaining_;
};

using RemoteAsyncDataProviderPtr = std::unique_ptr<RemoteAsyncDataProvider>;

} // namespace DataSource
} // namespace Config
} // namespace Envoy
