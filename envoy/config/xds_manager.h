#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/config/xds_config_tracker.h"
#include "envoy/config/xds_resources_delegate.h"
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
   * @param boostrap - the bootstrap config of Envoy.
   * @param cm - a pointer to a valid cluster manager.
   * @return Ok if the initialization was successful, or an error otherwise.
   */
  virtual absl::Status initialize(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                  Upstream::ClusterManager* cm) PURE;

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

  /**
   * Returns the XdsConfigTracker if defined by the bootstrap.
   * The object will be initialized (if configured) after the call to initialize().
   * TODO(adisuissa): this method will be removed once all the ADS-related objects
   * are moved out of the cluster-manager to the xds-manager.
   * @return the XdsConfigTracker if defined, or nullopt if not.
   */
  virtual OptRef<XdsConfigTracker> xdsConfigTracker() PURE;

  /**
   * Returns the XdsResourcesDelegate if defined by the bootstrap.
   * The object will be initialized (if configured) after the call to initialize().
   * TODO(adisuissa): this method will be removed once all the ADS-related objects
   * are moved out of the cluster-manager to the xds-manager.
   * @return the XdsResourcesDelegate if defined, or nullopt if not.
   */
  virtual XdsResourcesDelegateOptRef xdsResourcesDelegate() PURE;

  /**
   * Obtain the subscription factory for the cluster manager. Since subscriptions may have an
   * upstream component, the factory is a facet of the cluster manager.
   *
   * @return Config::SubscriptionFactory& the subscription factory.
   */
  virtual SubscriptionFactory& subscriptionFactory() PURE;
};

using XdsManagerPtr = std::unique_ptr<XdsManager>;
} // namespace Config
} // namespace Envoy
