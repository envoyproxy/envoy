#pragma once

#include <functional>

#include "envoy/api/api.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/init/manager.h"
#include "envoy/server/listener_manager.h"
#include "envoy/stats/scope.h"

#include "common/common/logger.h"
#include "common/init/target_impl.h"

namespace Envoy {
namespace Server {

/**
 * LDS API implementation that fetches via Subscription.
 */
class LdsApiImpl : public LdsApi,
                   Config::SubscriptionCallbacks<envoy::api::v2::Listener>,
                   Logger::Loggable<Logger::Id::upstream> {
public:
  LdsApiImpl(const envoy::api::v2::core::ConfigSource& lds_config, Upstream::ClusterManager& cm,
             Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
             Init::Manager& init_manager, const LocalInfo::LocalInfo& local_info,
             Stats::Scope& scope, ListenerManager& lm, Api::Api& api);

  // Server::LdsApi
  std::string versionInfo() const override { return version_info_; }

  // Config::SubscriptionCallbacks
  // TODO(fredlas) deduplicate
  void onConfigUpdate(const ResourceVector& resources, const std::string& version_info) override;
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>&,
                      const Protobuf::RepeatedPtrField<std::string>&, const std::string&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::Listener>(resource).name();
  }

private:
  std::unique_ptr<Config::Subscription<envoy::api::v2::Listener>> subscription_;
  std::string version_info_;
  ListenerManager& listener_manager_;
  Stats::ScopePtr scope_;
  Upstream::ClusterManager& cm_;
  Init::TargetImpl init_target_;
};

} // namespace Server
} // namespace Envoy
