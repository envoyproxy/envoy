#pragma once

#include <functional>

#include "envoy/api/v2/lds.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/init/init.h"
#include "envoy/server/listener_manager.h"
#include "envoy/stats/scope.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Server {

/**
 * LDS API implementation that fetches via Subscription.
 */
class LdsApiImpl : public LdsApi,
                   public Init::Target,
                   Config::SubscriptionCallbacks<envoy::api::v2::Listener>,
                   Logger::Loggable<Logger::Id::upstream> {
public:
  LdsApiImpl(const envoy::api::v2::core::ConfigSource& lds_config, Upstream::ClusterManager& cm,
             Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
             Init::Manager& init_manager, const LocalInfo::LocalInfo& local_info,
             Stats::Scope& scope, ListenerManager& lm);

  // Server::LdsApi
  std::string versionInfo() const override { return version_info_; }

  // Init::Target
  void initialize(std::function<void()> callback) override;

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const ResourceVector& resources, const std::string& version_info) override;
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::Listener>(resource).name();
  }

private:
  void runInitializeCallbackIfAny();

  std::unique_ptr<Config::Subscription<envoy::api::v2::Listener>> subscription_;
  std::string version_info_;
  ListenerManager& listener_manager_;
  Stats::ScopePtr scope_;
  Upstream::ClusterManager& cm_;
  std::function<void()> initialize_callback_;
};

} // namespace Server
} // namespace Envoy
