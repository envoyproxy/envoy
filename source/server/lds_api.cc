#include "server/lds_api.h"

#include <unordered_map>

#include "envoy/api/v2/lds.pb.validate.h"
#include "envoy/api/v2/listener/listener.pb.validate.h"

#include "common/common/cleanup.h"
#include "common/config/resources.h"
#include "common/config/subscription_factory.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"

#include "server/lds_subscription.h"

namespace Envoy {
namespace Server {

LdsApi::LdsApi(const envoy::api::v2::core::ConfigSource& lds_config, Upstream::ClusterManager& cm,
               Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
               Init::Manager& init_manager, const LocalInfo::LocalInfo& local_info,
               Stats::Scope& scope, ListenerManager& lm)
    : listener_manager_(lm), scope_(scope.createScope("listener_manager.lds.")), cm_(cm) {
  subscription_ =
      Envoy::Config::SubscriptionFactory::subscriptionFromConfigSource<envoy::api::v2::Listener>(
          lds_config, local_info.node(), dispatcher, cm, random, *scope_,
          [this, &lds_config, &cm, &dispatcher, &random,
           &local_info]() -> Config::Subscription<envoy::api::v2::Listener>* {
            return new LdsSubscription(Config::Utility::generateStats(*scope_), lds_config, cm,
                                       dispatcher, random, local_info);
          },
          "envoy.api.v2.ListenerDiscoveryService.FetchListeners",
          "envoy.api.v2.ListenerDiscoveryService.StreamListeners");
  Config::Utility::checkLocalInfo("lds", local_info);
  init_manager.registerTarget(*this);
}

void LdsApi::initialize(std::function<void()> callback) {
  initialize_callback_ = callback;
  subscription_->start({}, *this);
}

void LdsApi::onConfigUpdate(const ResourceVector& resources) {
  cm_.adsMux().pause(Config::TypeUrl::get().RouteConfiguration);
  Cleanup rds_resume([this] { cm_.adsMux().resume(Config::TypeUrl::get().RouteConfiguration); });
  for (const auto& listener : resources) {
    MessageUtil::validate(listener);
  }
  // We need to keep track of which listeners we might need to remove.
  std::unordered_map<std::string, std::reference_wrapper<Network::ListenerConfig>>
      listeners_to_remove;
  for (const auto& listener : listener_manager_.listeners()) {
    listeners_to_remove.emplace(listener.get().name(), listener);
  }

  for (const auto& listener : resources) {
    const std::string listener_name = listener.name();
    listeners_to_remove.erase(listener_name);
    try {
      if (listener_manager_.addOrUpdateListener(listener, true)) {
        ENVOY_LOG(info, "lds: add/update listener '{}'", listener_name);
      } else {
        ENVOY_LOG(debug, "lds: add/update listener '{}' skipped", listener_name);
      }
    } catch (const EnvoyException& e) {
      throw EnvoyException(
          fmt::format("Error adding/updating listener {}: {}", listener_name, e.what()));
    }
  }

  for (const auto& listener : listeners_to_remove) {
    if (listener_manager_.removeListener(listener.first)) {
      ENVOY_LOG(info, "lds: remove listener '{}'", listener.first);
    }
  }

  runInitializeCallbackIfAny();
}

void LdsApi::onConfigUpdateFailed(const EnvoyException*) {
  // We need to allow server startup to continue, even if we have a bad
  // config.
  runInitializeCallbackIfAny();
}

void LdsApi::runInitializeCallbackIfAny() {
  if (initialize_callback_) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

} // namespace Server
} // namespace Envoy
