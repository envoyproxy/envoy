#pragma once

#include "envoy/config/core/v3/config_source.pb.h"

namespace Envoy {
namespace Config {

/**
 * Configuration structure for bidirectional xDS.
 * This determines which resource providers should be enabled.
 */
struct BidirectionalXdsConfig {
  // Whether bidirectional xDS is enabled at all
  bool enabled = false;
  
  // Whether to provide listener readiness status
  bool provide_listener_status = true;
  
  // Whether to provide cluster health status  
  bool provide_cluster_status = false;
  
  // Whether to provide configuration snapshots
  bool provide_config_snapshots = false;
  
  /**
   * Create config from bootstrap configuration.
   * For now, we'll detect if ADS is configured and enable by default.
   */
  static BidirectionalXdsConfig fromBootstrap(const envoy::config::core::v3::ApiConfigSource& ads_config);
  
  /**
   * Create default config with listener status enabled.
   */
  static BidirectionalXdsConfig createDefault();
};

} // namespace Config
} // namespace Envoy 