#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "common/common/logger.h"
#include "common/upstream/egds_cluster_mapper.h"
#include "common/upstream/endpoint_groups_manager.h"

namespace Envoy {
namespace Upstream {

class EgdsApi {
public:
  virtual ~EgdsApi() = default;

  virtual void initialize(std::function<void()> initialization_fail_callback) PURE;
  virtual const std::string versionInfo() const PURE;
};

using EgdsApiPtr = std::unique_ptr<EgdsApi>;

/**
 * All SRDS stats. @see stats_macros.h
 */
// clang-format off
#define ALL_EGDS_STATS(COUNTER)                                                              \
  COUNTER(config_reload)                                                                     \
  COUNTER(update_empty)

// clang-format on

struct EgdsStats {
  ALL_EGDS_STATS(GENERATE_COUNTER_STRUCT)
};

class EgdsApiImpl : public Config::SubscriptionCallbacks,
                    public EgdsApi,
                    Logger::Loggable<Logger::Id::upstream> {
public:
  EgdsApiImpl(EndpointGroupsManager& endpoint_group_mananger,
              ProtobufMessage::ValidationVisitor& validation_visitor,
              Config::SubscriptionFactory& subscription_factory, Stats::Scope& scope,
              const Protobuf::RepeatedPtrField<envoy::config::endpoint::v3::Egds>& egds_list);
  ~EgdsApiImpl() override = default;

  // EgdsApi
  void initialize(std::function<void()> initialization_fail_callback) override;
  const std::string versionInfo() const override { return system_version_info_; }

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(
      const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
      const std::string& version_info) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::config::endpoint::v3::EndpointGroup>(resource).name();
  }

private:
  std::string loadTypeUrl(envoy::config::core::v3::ApiVersion resource_api_version);

  EndpointGroupsManager& endpoint_group_mananger_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  std::unordered_map<Config::SubscriptionPtr, std::set<std::string>> subscription_map_;
  std::string system_version_info_;
  Stats::ScopePtr scope_;
  EgdsStats stats_;
  std::function<void()> initialization_fail_callback_;
};

} // namespace Upstream
} // namespace Envoy
