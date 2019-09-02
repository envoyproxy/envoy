#pragma once

#include "envoy/api/api.h"
#include "envoy/api/v2/core/base.pb.h"
#include "envoy/init/manager.h"
#include "envoy/upstream/cluster_manager.h"

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
std::string read(const envoy::api::v2::core::DataSource& source, bool allow_empty, Api::Api& api);

/**
 * @param source data source.
 * @return absl::optional<std::string> path to DataSource if a filename, otherwise absl::nullopt.
 */
absl::optional<std::string> getPath(const envoy::api::v2::core::DataSource& source);

/**
 * Callback for async data source.
 */
using AsyncDataSourceCb = std::function<void(const std::string&)>;

class LocalAsyncDataProvider {
public:
  LocalAsyncDataProvider(Init::Manager& manager, const envoy::api::v2::core::DataSource& source,
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

class RemoteAsyncDataProvider : public Config::DataFetcher::RemoteDataFetcherCallback {
public:
  RemoteAsyncDataProvider(Upstream::ClusterManager& cm, Init::Manager& manager,
                          const envoy::api::v2::core::RemoteDataSource& source, bool allow_empty,
                          AsyncDataSourceCb&& callback)
      : allow_empty_(allow_empty), callback_(std::move(callback)),
        fetcher_(std::make_unique<Config::DataFetcher::RemoteDataFetcher>(cm, source.http_uri(),
                                                                          source.sha256(), *this)),
        init_target_("RemoteAsyncDataProvider", [this]() { start(); }) {
    manager.add(init_target_);
  }

  ~RemoteAsyncDataProvider() override { init_target_.ready(); }

  // Config::DataFetcher::RemoteDataFetcherCallback
  void onSuccess(const std::string& data) override {
    callback_(data);
    init_target_.ready();
  }

  // Config::DataFetcher::RemoteDataFetcherCallback
  void onFailure(Config::DataFetcher::FailureReason failure) override {
    if (allow_empty_) {
      callback_(EMPTY_STRING);
      init_target_.ready();
    } else {
      throw EnvoyException(
          fmt::format("Failed to fetch remote data. Failure reason: {}", enumToInt(failure)));
    }
  }

private:
  void start() { fetcher_->fetch(); }

  bool allow_empty_;
  AsyncDataSourceCb callback_;
  const Config::DataFetcher::RemoteDataFetcherPtr fetcher_;
  Init::TargetImpl init_target_;
};

using RemoteAsyncDataProviderPtr = std::unique_ptr<RemoteAsyncDataProvider>;

} // namespace DataSource
} // namespace Config
} // namespace Envoy
