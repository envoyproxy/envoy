#pragma once

#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/config/resource_type_helper.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/upstream/cds_api_helper.h"

namespace Envoy {
namespace Upstream {

/**
 * All ODCDS stats. @see stats_macros.h
 */
#define ALL_ODCDS_STATS(COUNTER) COUNTER(known_missing_answers)

/**
 * Struct definition for all ODCDS stats. @see stats_macros.h
 */
struct OdCdsStats {
  ALL_ODCDS_STATS(GENERATE_COUNTER_STRUCT)
};

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
                     public Config::SubscriptionCallbacks,
                     Logger::Loggable<Logger::Id::upstream> {
public:
  static absl::StatusOr<OdCdsApiSharedPtr>
  create(const envoy::config::core::v3::ConfigSource& odcds_config,
         OptRef<xds::core::v3::ResourceLocator> odcds_resources_locator,
         Config::XdsManager& xds_manager, ClusterManager& cm, MissingClusterNotifier& notifier,
         Stats::Scope& scope, ProtobufMessage::ValidationVisitor& validation_visitor,
         Server::Configuration::ServerFactoryContext& server_factory_context);

  // Upstream::OdCdsApi
  void updateOnDemand(std::string cluster_name) override;
  bool isKnownMissing(absl::string_view cluster_name) const override {
    return missing_names_.contains(cluster_name);
  }
  void recordKnownMissingAnswer() override { stats_.known_missing_answers_.inc(); }

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
               OptRef<xds::core::v3::ResourceLocator> odcds_resources_locator,
               Config::XdsManager& xds_manager, ClusterManager& cm,
               MissingClusterNotifier& notifier, Stats::Scope& scope,
               ProtobufMessage::ValidationVisitor& validation_visitor,
               absl::Status& creation_status);
  void sendAwaiting();

  CdsApiHelper helper_;
  MissingClusterNotifier& notifier_;
  Stats::ScopeSharedPtr scope_;
  OdCdsStats stats_;
  StartStatus status_{StartStatus::NotStarted};
  absl::flat_hash_set<std::string> awaiting_names_;
  // The names requested on demand through this instance. The delta subscription keeps the same
  // names in its interest, so this mirrors state the subscription already holds and is re-sent
  // to the server when the stream reconnects.
  absl::flat_hash_set<std::string> requested_names_;
  // The requested names the server has answered with "doesn't exist" (via removed_resources)
  // and that no later update has delivered.
  absl::flat_hash_set<std::string> missing_names_;
  const Config::ResourceTypeHelper<envoy::config::cluster::v3::Cluster> resource_type_helper_;
  Config::SubscriptionPtr subscription_;
};

/**
 * ODCDS API implementation that fetches via Subscription for xDS-TP based
 * configs and resources.
 */
class XdstpOdCdsApiImpl : public OdCdsApi {
public:
  static absl::StatusOr<OdCdsApiSharedPtr>
  create(const envoy::config::core::v3::ConfigSource&, OptRef<xds::core::v3::ResourceLocator>,
         Config::XdsManager& xds_manager, ClusterManager& cm, MissingClusterNotifier& notifier,
         Stats::Scope& scope, ProtobufMessage::ValidationVisitor& validation_visitor,
         Server::Configuration::ServerFactoryContext& server_factory_context);

  // Upstream::OdCdsApi
  void updateOnDemand(std::string cluster_name) override;
  bool isKnownMissing(absl::string_view cluster_name) const override;
  void recordKnownMissingAnswer() override;

private:
  class XdstpOdcdsSubscriptionsManager;
  using XdstpOdcdsSubscriptionsManagerSharedPtr = std::shared_ptr<XdstpOdcdsSubscriptionsManager>;

  XdstpOdCdsApiImpl(Config::XdsManager& xds_manager, ClusterManager& cm,
                    MissingClusterNotifier& notifier, Stats::Scope& scope,
                    Server::Configuration::ServerFactoryContext& server_context, bool old_ads,
                    ProtobufMessage::ValidationVisitor& validation_visitor,
                    absl::Status& creation_status);

  // Fetches, and potentially creates, the singleton subscriptions manager.
  // The arguments will be passed to the subscriptions manager's constructor, if
  // it is the first time it is initialized.
  static XdstpOdcdsSubscriptionsManagerSharedPtr
  subscriptionsManager(Server::Configuration::ServerFactoryContext& context,
                       Config::XdsManager& xds_manager, ClusterManager& cm,
                       MissingClusterNotifier& notifier, Stats::Scope& scope,
                       ProtobufMessage::ValidationVisitor& validation_visitor);

  // A singleton through which all subscriptions will be processed.
  XdstpOdcdsSubscriptionsManagerSharedPtr subscriptions_manager_;
  const bool old_ads_;
};

} // namespace Upstream
} // namespace Envoy
