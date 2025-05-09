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
   * Initializes the ADS connections.
   * This should be called after the cluster-manager was created, and the
   * primiary clusters were initialized.
   * @param boostrap - the bootstrap config of Envoy.
   * @return Ok if the initialization was successful, or an error otherwise.
   */
  virtual absl::Status
  initializeAdsConnections(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) PURE;

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
   * Returns a shared_ptr to the singleton xDS-over-gRPC provider for upstream control plane muxing
   * of xDS. This is treated somewhat as a special case in ClusterManager, since it does not relate
   * logically to the management of clusters but instead is required early in ClusterManager/server
   * initialization and in various sites that need ClusterManager for xDS API interfacing.
   *
   * @return GrpcMux& ADS API provider referencee.
   */
  virtual Config::GrpcMuxSharedPtr adsMux() PURE;

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
