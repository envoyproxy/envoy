#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/singleton/instance.h"
#include "envoy/singleton/manager.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/config/remote_data_fetcher.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

/**
 * A process-wide singleton that manages background HTTP fetches for remote dynamic modules
 * in nack_on_cache_miss mode. Keyed by SHA256 to deduplicate fetches across factories and
 * listeners referencing the same module.
 *
 * All access is on the main thread (xDS config processing), so no mutex is needed.
 */
class BackgroundFetchManager : public Singleton::Instance {
public:
  /**
   * Returns the process-wide singleton, creating it on first access.
   */
  static std::shared_ptr<BackgroundFetchManager> singleton(Singleton::Manager& manager);

  /**
   * Removes any fetch state for this SHA256 (e.g., after a successful cache hit).
   */
  void erase(const std::string& sha256);

  /**
   * Cleans up completed fetches and starts a new one if no fetch is in-flight for this SHA256.
   */
  void fetchIfNeeded(const std::string& sha256, Upstream::ClusterManager& cm,
                     const envoy::config::core::v3::RemoteDataSource& source);

private:
  struct BackgroundFetchState : public Config::DataFetcher::RemoteDataFetcherCallback,
                                public Logger::Loggable<Logger::Id::dynamic_modules> {
    BackgroundFetchState(Upstream::ClusterManager& cm,
                         const envoy::config::core::v3::RemoteDataSource& source);

    // Config::DataFetcher::RemoteDataFetcherCallback
    void onSuccess(const std::string& data) override;
    void onFailure(Config::DataFetcher::FailureReason reason) override;

    Config::DataFetcher::RemoteDataFetcherPtr fetcher_;
    std::string sha256_;
    bool completed_{false};
  };

  absl::flat_hash_map<std::string, std::unique_ptr<BackgroundFetchState>> background_fetches_;
};

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
