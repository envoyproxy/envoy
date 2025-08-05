#include "source/common/config/bidirectional_xds_config.h"

#include "envoy/config/core/v3/config_source.pb.h"

namespace Envoy {
namespace Config {

BidirectionalXdsConfig BidirectionalXdsConfig::fromBootstrap(
    const envoy::config::core::v3::ApiConfigSource& ads_config) {
  BidirectionalXdsConfig config;
  
  // Enable bidirectional xDS if ADS is configured
  if (ads_config.api_type() == envoy::config::core::v3::ApiConfigSource::GRPC ||
      ads_config.api_type() == envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC) {
    config.enabled = true;
    config.provide_listener_status = true;
    
    // Future: Parse additional configuration from ads_config extensions
    // For now, use safe defaults
    config.provide_cluster_status = false;
    config.provide_config_snapshots = false;
  }
  
  return config;
}

BidirectionalXdsConfig BidirectionalXdsConfig::createDefault() {
  BidirectionalXdsConfig config;
  config.enabled = true;
  config.provide_listener_status = true;
  config.provide_cluster_status = false;
  config.provide_config_snapshots = false;
  return config;
}

} // namespace Config
} // namespace Envoy