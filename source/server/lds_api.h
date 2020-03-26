#pragma once

#include <functional>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/discovery_service_base.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/init/manager.h"
#include "envoy/server/listener_manager.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "common/common/logger.h"
#include "common/init/target_impl.h"

namespace Envoy {
namespace Server {

/**
 * LDS API implementation that fetches via Subscription.
 */
class LdsApiImpl : public LdsApi,
                   Envoy::Config::SubscriptionBase<envoy::config::listener::v3::Listener>,
                   Logger::Loggable<Logger::Id::upstream> {
public:
  LdsApiImpl(const envoy::config::core::v3::ConfigSource& lds_config, Upstream::ClusterManager& cm,
             Init::Manager& init_manager, Stats::Scope& scope, ListenerManager& lm,
             ProtobufMessage::ValidationVisitor& validation_visitor);

  // Server::LdsApi
  std::string versionInfo() const override { return system_version_info_; }

private:
  // Config::SubscriptionCallbacks
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(
      const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
      const std::string& system_version_info) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::config::listener::v3::Listener>(resource).name();
  }

  std::unique_ptr<Config::Subscription> subscription_;
  std::string system_version_info_;
  ListenerManager& listener_manager_;
  Stats::ScopePtr scope_;
  Upstream::ClusterManager& cm_;
  Init::TargetImpl init_target_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

} // namespace Server
} // namespace Envoy
