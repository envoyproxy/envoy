#pragma once

#include "envoy/config/ads.h"
#include "envoy/config/subscription.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"

#include "api/discovery.pb.h"

namespace Envoy {
namespace Config {

/**
 * ADS API implementation that fetches via gRPC.
 * TODO(htuch): Implement ADS. This should look similar to GrpcSubscriptionImpl, except it manages
 * multiple in-flight DiscoveryRequests, one per type URL.
 */
class AdsApiImpl : public AdsApi, Logger::Loggable<Logger::Id::upstream> {
public:
  AdsApiImpl(const envoy::api::v2::ApiConfigSource& ads_config,
             Upstream::ClusterManager& cluster_manager) {
    UNREFERENCED_PARAMETER(ads_config);
    UNREFERENCED_PARAMETER(cluster_manager);
  }

  AdsWatchPtr subscribe(const std::string& type_url, const std::vector<std::string>& resources,
                        AdsCallbacks& callbacks) override {
    UNREFERENCED_PARAMETER(type_url);
    UNREFERENCED_PARAMETER(resources);
    UNREFERENCED_PARAMETER(callbacks);
    return nullptr;
  }
};

} // namespace Config
} // namespace Envoy
