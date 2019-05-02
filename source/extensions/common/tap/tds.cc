#include "extensions/common/tap/tds.h"
#include "envoy/service/tap/v2alpha/tds.pb.validate.h"
#include "common/config/utility.h"
#include "common/config/subscription_factory.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

TdsTapConfigSubscriptionHandle::~TdsTapConfigSubscriptionHandle(){}

TdsTapConfigSubscriptionHandlePtr TapConfigProviderManagerImpl::subscribeTap(
      const envoy::config::common::tap::v2alpha::CommonExtensionConfig_TDSConfig& tds,

      /* Server::Configuration::FactoryContext& factory_context  be explicit here ... */
      Extensions::Common::Tap::ExtensionConfig& ptr,

      const std::string& stat_prefix,
      Stats::Scope& stats,
      Upstream::ClusterManager& cluster_Manager,
      const LocalInfo::LocalInfo& local_info,
      Event::Dispatcher&  main_thread_dispatcher,
      Envoy::Runtime::RandomGenerator& random,
      Api::Api& api
      ) {
  const uint64_t manager_identifier = MessageUtil::hash(tds);

TdsTapConfigSubscriptionSharedPtr subscription;

  auto it = tap_config_subscriptions_.find(manager_identifier);
  if (it == tap_config_subscriptions_.end()) {
    // std::make_shared does not work for classes with private constructors. There are ways
    // around it. However, since this is not a performance critical path we err on the side
    // of simplicity.
    subscription.reset(new TdsTapConfigSubscription(
        tds,
        manager_identifier,
        *this,

stat_prefix,
stats,
cluster_Manager,
local_info,
main_thread_dispatcher,
random,
api
    ));
    

    // TODO: i think i need this but not sure.
    // What do I do if it's nullptr?
    init_manager_->add(subscription->init_target_);

    tap_config_subscriptions_.insert({manager_identifier, subscription});
  } else {
    // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
    // in the TdsRouteConfigSubscription destructor, and the single threaded nature
    // of this code, locking the weak_ptr will not fail.
    subscription = it->second.lock();
  }
  ASSERT(subscription);

    return std::make_unique<TdsTapConfigSubscriptionHandleImpl>(subscription, ptr);
}

TdsTapConfigSubscriptionHandleImpl::TdsTapConfigSubscriptionHandleImpl(TdsTapConfigSubscriptionSharedPtr sub,
  ExtensionConfig& config) : sub_(sub),config_(config) {
    sub_->add(config_);
}

TdsTapConfigSubscriptionHandleImpl::~TdsTapConfigSubscriptionHandleImpl() {
    sub_->remove(config_);
}

///////////////////////////
TdsTapConfigSubscription::TdsTapConfigSubscription(

      const envoy::config::common::tap::v2alpha::CommonExtensionConfig_TDSConfig& tds,
      const uint64_t manager_identifier,
      TapConfigProviderManagerImpl& tap_config_provider_manager,

      const std::string& stat_prefix,
      Stats::Scope& stats,
      Upstream::ClusterManager& cluster_Manager,
      const LocalInfo::LocalInfo& local_info,
      Event::Dispatcher&  main_thread_dispatcher,
      Envoy::Runtime::RandomGenerator& random,
      Api::Api& api
)
: tap_config_name_(tds.name()),
      init_target_(fmt::format("TdsTapConfigSubscription {}", tap_config_name_),
                   [this]() { subscription_->start({tap_config_name_}, *this); }),
      scope_(stats.createScope(stat_prefix + "tds." + tap_config_name_ + ".")),
      stats_({ALL_TDS_STATS(POOL_COUNTER(*scope_))}),
      tap_config_provider_manager_(tap_config_provider_manager),
      manager_identifier_(manager_identifier) ,
      last_updated_(api.timeSource().systemTime() ), api_(api) {
  Envoy::Config::Utility::checkLocalInfo("tds", local_info);

  subscription_ = Envoy::Config::SubscriptionFactory::subscriptionFromConfigSource(
      tds.config_source(), local_info, main_thread_dispatcher,
      cluster_Manager, random, *scope_,
      "envoy.service.tap.v2alpha.TapDiscoveryService.FetchTapConfigs",
      "envoy.service.tap.v2alpha.TapDiscoveryService.StreamTapConfigs",
      Grpc::Common::typeUrl(envoy::service::tap::v2alpha::TapConfiguration().GetDescriptor()->full_name()),
      api);
}


TdsTapConfigSubscription::~TdsTapConfigSubscription() {
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  init_target_.ready();

  tap_config_provider_manager_.tap_config_subscriptions_.erase(manager_identifier_);
}

void TdsTapConfigSubscription::onConfigUpdate(
    const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
    const std::string& version_info) {
  last_updated_ = api_.timeSource().systemTime();

  if (resources.empty()) {
    ENVOY_LOG(debug, "Missing TapConfig for {} in onConfigUpdate()", tap_config_name_);

    for (auto* provider : tap_extension_configs_) {
      provider->clearTapConfig();
    }

    init_target_.ready();
    return;
  }


  if (resources.size() != 1) {
    throw EnvoyException(fmt::format("Unexpected TDS resource length: {}", resources.size()));
  }
  auto tap_config = MessageUtil::anyConvert<envoy::service::tap::v2alpha::TapConfiguration>(resources[0]);
  MessageUtil::validate(tap_config);

  if (!(tap_config.name() == tap_config_name_)) {
    throw EnvoyException(fmt::format("Unexpected TDS configuration (expecting {}): {}",
                                     tap_config_name_, tap_config.name()));
  }

  const uint64_t new_hash = MessageUtil::hash(tap_config);
  if (!config_info_ || new_hash != config_info_.value().last_config_hash_) {
    config_info_ = {new_hash, version_info};
    tap_config_proto_ = tap_config.config();
    stats_.config_reload_.inc();
    ENVOY_LOG(debug, "Tds: loading new configuration: config_name={} hash={}", tap_config_name_,
              new_hash);
    for (auto* provider : tap_extension_configs_) {
      auto tap_config_proto_copy = tap_config_proto_; 
      provider->newTapConfig(std::move(tap_config_proto_copy), nullptr);
    }
  }

  init_target_.ready();
}

void TdsTapConfigSubscription::onConfigUpdateFailed(const EnvoyException*) {
  // We need to allow server startup to continue, even if we have a bad
  // config.
  init_target_.ready();
}

}
}
}
}