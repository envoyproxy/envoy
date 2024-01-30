#pragma once

#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/config/subscription_base.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/upstream/cds_api_helper.h"

namespace Envoy {
namespace Upstream {

enum class StartStatus {
  // No initial fetch started.
  NotStarted,
  // Initial fetch started.
  Started,
  // Initial fetch arrived.
  InitialFetchDone,
};

/**
 * An interface for on-demand CDS. Defined to allow mocking.
 */
class OdCdsApi {
public:
  virtual ~OdCdsApi() = default;

  // Subscribe to a cluster with a given name. It's meant to eventually send a discovery request
  // with the cluster name to the management server.
  virtual void updateOnDemand(std::string cluster_name) PURE;
};

using OdCdsApiSharedPtr = std::shared_ptr<OdCdsApi>;

/**
 * An interface used by OdCdsApiImpl for sending notifications about the missing cluster that was
 * requested.
 */
class MissingClusterNotifier {
public:
  virtual ~MissingClusterNotifier() = default;

  virtual void notifyMissingCluster(absl::string_view name) PURE;
};

/**
 * ODCDS API implementation that fetches via Subscription.
 */
class OdCdsApiImpl : public OdCdsApi,
                     Envoy::Config::SubscriptionBase<envoy::config::cluster::v3::Cluster>,
                     Logger::Loggable<Logger::Id::upstream> {
public:
  static OdCdsApiSharedPtr create(const envoy::config::core::v3::ConfigSource& odcds_config,
                                  OptRef<xds::core::v3::ResourceLocator> odcds_resources_locator,
                                  ClusterManager& cm, MissingClusterNotifier& notifier,
                                  Stats::Scope& scope,
                                  ProtobufMessage::ValidationVisitor& validation_visitor);

  // Upstream::OdCdsApi
  void updateOnDemand(std::string cluster_name) override;

private:
  // Config::SubscriptionCallbacks
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                              const std::string& version_info) override;
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string& system_version_info) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;

  OdCdsApiImpl(const envoy::config::core::v3::ConfigSource& odcds_config,
               OptRef<xds::core::v3::ResourceLocator> odcds_resources_locator, ClusterManager& cm,
               MissingClusterNotifier& notifier, Stats::Scope& scope,
               ProtobufMessage::ValidationVisitor& validation_visitor);
  void sendAwaiting();

  CdsApiHelper helper_;
  ClusterManager& cm_;
  MissingClusterNotifier& notifier_;
  Stats::ScopeSharedPtr scope_;
  StartStatus status_{StartStatus::NotStarted};
  absl::flat_hash_set<std::string> awaiting_names_;
  Config::SubscriptionPtr subscription_;
};

} // namespace Upstream
} // namespace Envoy
