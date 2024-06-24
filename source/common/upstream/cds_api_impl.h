#pragma once

#include <functional>
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

/**
 * CDS API implementation that fetches via Subscription.
 */
class CdsApiImpl : public CdsApi,
                   Envoy::Config::SubscriptionBase<envoy::config::cluster::v3::Cluster> {
public:
  static CdsApiPtr create(const envoy::config::core::v3::ConfigSource& cds_config,
                          const xds::core::v3::ResourceLocator* cds_resources_locator,
                          ClusterManager& cm, Stats::Scope& scope,
                          ProtobufMessage::ValidationVisitor& validation_visitor);

  // Upstream::CdsApi
  void initialize() override { subscription_->start({}); }
  void setInitializedCb(std::function<void()> callback) override {
    initialize_callback_ = callback;
  }
  const std::string versionInfo() const override { return helper_.versionInfo(); }

private:
  // Config::SubscriptionCallbacks
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                              const std::string& version_info) override;
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string& system_version_info) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;
  CdsApiImpl(const envoy::config::core::v3::ConfigSource& cds_config,
             const xds::core::v3::ResourceLocator* cds_resources_locator, ClusterManager& cm,
             Stats::Scope& scope, ProtobufMessage::ValidationVisitor& validation_visitor);
  void runInitializeCallbackIfAny();

  CdsApiHelper helper_;
  ClusterManager& cm_;
  Stats::ScopeSharedPtr scope_;
  Config::SubscriptionPtr subscription_;
  std::function<void()> initialize_callback_;
};

} // namespace Upstream
} // namespace Envoy
