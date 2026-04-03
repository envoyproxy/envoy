#include "source/extensions/dynamic_modules/background_fetch_manager.h"

#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

SINGLETON_MANAGER_REGISTRATION(dynamic_module_background_fetch_manager);

std::shared_ptr<BackgroundFetchManager>
BackgroundFetchManager::singleton(Singleton::Manager& manager) {
  return manager.getTyped<BackgroundFetchManager>(
      SINGLETON_MANAGER_REGISTERED_NAME(dynamic_module_background_fetch_manager),
      [] { return std::make_shared<BackgroundFetchManager>(); },
      /*pin=*/true);
}

void BackgroundFetchManager::erase(const std::string& sha256) { background_fetches_.erase(sha256); }

void BackgroundFetchManager::fetchIfNeeded(
    const std::string& sha256, Upstream::ClusterManager& cm,
    const envoy::config::core::v3::RemoteDataSource& source) {
  auto it = background_fetches_.find(sha256);
  if (it != background_fetches_.end() && it->second->completed_) {
    background_fetches_.erase(it);
    it = background_fetches_.end();
  }
  if (it == background_fetches_.end()) {
    background_fetches_.emplace(sha256, std::make_unique<BackgroundFetchState>(cm, source));
  }
}

BackgroundFetchManager::BackgroundFetchState::BackgroundFetchState(
    Upstream::ClusterManager& cm, const envoy::config::core::v3::RemoteDataSource& source)
    : sha256_(source.sha256()) {
  fetcher_ = std::make_unique<Config::DataFetcher::RemoteDataFetcher>(cm, source.http_uri(),
                                                                      source.sha256(), *this);
  fetcher_->fetch();
}

void BackgroundFetchManager::BackgroundFetchState::onSuccess(const std::string& data) {
  auto status = writeDynamicModuleBytesToDisk(data, sha256_);
  if (!status.ok()) {
    ENVOY_LOG(error, "Failed to write background-fetched module to disk: {}", status.message());
  } else {
    ENVOY_LOG(info, "Background fetch complete, module cached for SHA256 {}", sha256_);
  }
  completed_ = true;
}

void BackgroundFetchManager::BackgroundFetchState::onFailure(
    Config::DataFetcher::FailureReason reason) {
  ENVOY_LOG(error, "Background fetch failed for SHA256 {}, reason: {}", sha256_,
            static_cast<int>(reason));
  completed_ = true;
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
