#pragma once

#include <string>

#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/init/manager.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/backoff_strategy.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/config/oci_image_blob_fetcher.h"
#include "source/common/config/oci_image_manifest_fetcher.h"
#include "source/common/init/target_impl.h"

#include "absl/types/optional.h"

namespace Envoy {

/**
 * Callback for async request for OCI image manifest.
 */
using OciManifestCb = std::function<void(const std::string&)>;

class OciManifestProvider : public Event::DeferredDeletable,
                            public Config::DataFetcher::RemoteDataFetcherCallback,
                            public Logger::Loggable<Logger::Id::config> {
public:
  OciManifestProvider(Upstream::ClusterManager& cm, Init::Manager& manager,
                      const envoy::config::core::v3::HttpUri uri, std::string token,
                      std::string sha256, bool allow_empty, OciManifestCb&& callback);

  ~OciManifestProvider() override {
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
    ENVOY_LOG(debug, "Failed to fetch OCI manifest, failure reason: {}", enumToInt(failure));
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
  OciManifestCb callback_;
  const Config::DataFetcher::OciImageManifestFetcherPtr fetcher_;
  Init::TargetImpl init_target_;

  Event::TimerPtr retry_timer_;
  BackOffStrategyPtr backoff_strategy_;
  uint32_t retries_remaining_;
};

using OciManifestProviderPtr = std::unique_ptr<OciManifestProvider>;

/**
 * Callback for async request for OCI image blob.
 */
using OciBlobCb = std::function<void(const std::string&)>;

class OciBlobProvider : public Event::DeferredDeletable,
                        public Config::DataFetcher::RemoteDataFetcherCallback,
                        public Logger::Loggable<Logger::Id::config> {
public:
  OciBlobProvider(Upstream::ClusterManager& cm, Init::Manager& manager,
                  const envoy::config::core::v3::HttpUri uri, std::string token, std::string digest,
                  std::string sha256, bool allow_empty, OciBlobCb&& callback);

  ~OciBlobProvider() override {
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
    ENVOY_LOG(debug, "Failed to fetch OCI image blob, failure reason: {}", enumToInt(failure));
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
  OciBlobCb callback_;
  const Config::DataFetcher::OciImageBlobFetcherPtr fetcher_;
  Init::TargetImpl init_target_;

  Event::TimerPtr retry_timer_;
  BackOffStrategyPtr backoff_strategy_;
  uint32_t retries_remaining_;
};

using OciBlobProviderPtr = std::unique_ptr<OciBlobProvider>;

} // namespace Envoy
