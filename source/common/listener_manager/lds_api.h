#pragma once

#include <functional>

#include "envoy/common/random_generator.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/init/manager.h"
#include "envoy/server/listener_manager.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/logger.h"
#include "source/common/init/target_impl.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Server {

/**
 * LDS API implementation that fetches via Subscription.
 */
class LdsApiImpl : public LdsApi,
                   public Config::SubscriptionCallbacks,
                   Logger::Loggable<Logger::Id::upstream> {
public:
  LdsApiImpl(const envoy::config::core::v3::ConfigSource& lds_config,
             const xds::core::v3::ResourceLocator* lds_resources_locator,
             Upstream::ClusterManager& cm, Init::Manager& init_manager, Stats::Scope& scope,
             ListenerManager& lm, ProtobufMessage::ValidationVisitor& validation_visitor,
             Random::RandomGenerator& random);

  // Server::LdsApi
  std::string versionInfo() const override { return system_version_info_; }

private:
  // Config::SubscriptionCallbacks
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                              const std::string& version_info) override;
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string& system_version_info) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;

  Config::SubscriptionPtr subscription_;
  std::string system_version_info_;
  ListenerManager& listener_manager_;
  Stats::ScopeSharedPtr scope_;
  const Config::OpaqueResourceDecoderSharedPtr resource_decoder_;
  Init::TargetImpl init_target_;
  // The names of the listeners that the last SotW update delivered, without the ones whose TTL
  // expired since.
  absl::flat_hash_set<std::string> sotw_listener_names_;
};

} // namespace Server
} // namespace Envoy
