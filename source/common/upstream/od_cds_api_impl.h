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
 * ODCDS API implementation that fetches via Subscription.
 */
class OdCdsApiImpl : public OdCdsApi,
                     Envoy::Config::SubscriptionBase<envoy::config::cluster::v3::Cluster>,
                     Logger::Loggable<Logger::Id::upstream> {
public:
  static absl::StatusOr<OdCdsApiSharedPtr>
  create(const envoy::config::core::v3::ConfigSource& odcds_config,
         OptRef<xds::core::v3::ResourceLocator> odcds_resources_locator, ClusterManager& cm,
         MissingClusterNotifier& notifier, Stats::Scope& scope,
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
               ProtobufMessage::ValidationVisitor& validation_visitor,
               absl::Status& creation_status);
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
