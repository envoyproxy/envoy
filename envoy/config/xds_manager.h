#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Config {

/**
 * An xDS-Manager interface for all operations related to xDS connections and
 * resources in Envoy.
 *
 * This class is WIP. Currently supported functionality:
 * - Dynamically set the ADS configuration to be used.
 *
 * In general, this is intended to be used only on the main thread, as part of the Server instance
 * interface and config subsystem.
 */
class XdsManager {
public:
  virtual ~XdsManager() = default;

  /**
   * Initializes the xDS-Manager.
   * This should be called after the cluster-manager is created.
   * @param cm - a pointer to a valid cluster manager.
   * @return Ok if the initialization was successful, or an error otherwise.
   */
  virtual absl::Status initialize(Upstream::ClusterManager* cm) PURE;

  /**
   * Shuts down the xDS-Manager and all the configured connections to the config
   * servers.
   */
  virtual void shutdown() PURE;

  /**
   * Set the ADS ConfigSource Envoy should use that will replace the current ADS
   * server.
   * @param ads_config the ADS config of the new server.
   * @return Ok if the ADS config is valid (points to a valid static server),
   *         or an error otherwise.
   */
  virtual absl::Status
  setAdsConfigSource(const envoy::config::core::v3::ApiConfigSource& config_source) PURE;
};

using XdsManagerPtr = std::unique_ptr<XdsManager>;
} // namespace Config
} // namespace Envoy
