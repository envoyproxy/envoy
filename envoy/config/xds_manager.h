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
   * @param bootstrap - the bootstrap config of Envoy.
   * @param cm - a pointer to a valid cluster manager.
   * @return Ok if the initialization was successful, or an error otherwise.
   */
  virtual absl::Status initialize(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                  Upstream::ClusterManager* cm) PURE;

  /**
   * Initializes the ADS connections.
   * This should be called after the cluster-manager was created, and the
   * primiary clusters were initialized.
   * @param bootstrap - the bootstrap config of Envoy.
   * @return Ok if the initialization was successful, or an error otherwise.
   */
  virtual absl::Status
  initializeAdsConnections(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) PURE;

  /**
   * Start all xDS-TP config-based gRPC muxes (if any).
   * This includes both the servers defined in the `config_sources`, and
   * `default_config_source` in the bootstrap.
   */
  virtual void startXdstpAdsMuxes() PURE;

  /**
   * Subscription to a singleton resource.
   * This will create a subscription to a singleton resource, based on the resource_name and the
   * config source. If an xDS-TP based resource name is given, then the config sources defined in
   * the Bootstrap config_sources/default_config_source may be used.
   *
   * @param resource_name absl::string_view the resource to subscribe to.
   * @param config OptRef<const envoy::config::core::v3::ConfigSource> an optional config source to
   * use.
   * @param type_url type URL for the resource being subscribed to.
   * @param scope stats scope for any stats tracked by the subscription.
   * @param callbacks the callbacks needed by all Subscription objects, to deliver config updates.
   *                  The callbacks must not result in the deletion of the Subscription object.
   * @param resource_decoder how incoming opaque resource objects are to be decoded.
   * @param options subscription options.
   *
   * @return SubscriptionPtr subscription object corresponding for config and type_url or error
   * status.
   */
  virtual absl::StatusOr<SubscriptionPtr> subscribeToSingletonResource(
      absl::string_view resource_name, OptRef<const envoy::config::core::v3::ConfigSource> config,
      absl::string_view type_url, Stats::Scope& scope, SubscriptionCallbacks& callbacks,
      OpaqueResourceDecoderSharedPtr resource_decoder, const SubscriptionOptions& options) PURE;

  /**
   * Pause discovery requests for a given API type on all ADS types (both xdstp-based and "old"
   * ADS). This is useful, for example, when we're processing an update for LDS or CDS and don't
   * want a flood of updates for RDS or EDS respectively. Discovery requests may later be resumed
   * with after the returned ScopedResume object is destroyed.
   * @param type_url type URL corresponding to xDS API, e.g.
   * type.googleapis.com/envoy.config.cluster.v3.Cluster.
   *
   * @return a ScopedResume object, which when destructed, resumes the paused discovery requests.
   * A discovery request will be sent if one would have been sent during the pause.
   */
  ABSL_MUST_USE_RESULT virtual ScopedResume pause(const std::string& type_url) PURE;

  /**
   * Pause discovery requests for given API types on all ADS types (both xdstp-based and "old" ADS).
   * This is useful, for example, when we're processing an update for LDS or CDS and don't want a
   * flood of updates for RDS or EDS respectively. Discovery requests may later be resumed with
   * after the returned ScopedResume object is destroyed.
   * @param type_urls type URLs corresponding to xDS API, e.g.
   * type.googleapis.com/envoy.config.cluster.v3.Cluster.
   *
   * @return a ScopedResume object, which when destructed, resumes the paused discovery requests.
   * A discovery request will be sent if one would have been sent during the pause.
   */
  ABSL_MUST_USE_RESULT virtual ScopedResume pause(const std::vector<std::string>& type_urls) PURE;

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
