#include "common/router/rds_impl.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/api/v2/rds.pb.validate.h"
#include "envoy/api/v2/route/route.pb.validate.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/rds_json.h"
#include "common/config/subscription_factory.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"
#include "common/router/config_impl.h"
#include "common/router/rds_subscription.h"

namespace Envoy {
namespace Router {

RouteConfigProviderSharedPtr RouteConfigProviderUtil::create(
    const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
        config,
    Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix,
    RouteConfigProviderManager& route_config_provider_manager) {
  switch (config.route_specifier_case()) {
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      kRouteConfig:
    return route_config_provider_manager.getStaticRouteConfigProvider(config.route_config(),
                                                                      factory_context);
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::kRds:
    return route_config_provider_manager.getRdsRouteConfigProvider(config.rds(), factory_context,
                                                                   stat_prefix);
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

StaticRouteConfigProviderImpl::StaticRouteConfigProviderImpl(
    const envoy::api::v2::RouteConfiguration& config,
    Server::Configuration::FactoryContext& factory_context)
    : config_(new ConfigImpl(config, factory_context, true)), route_config_proto_{config},
      last_updated_(factory_context.systemTimeSource().currentTime()) {}

// TODO(htuch): If support for multiple clusters is added per #1170 cluster_name_
// initialization needs to be fixed.
RdsRouteConfigSubscription::RdsRouteConfigSubscription(
    const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
    const std::string& manager_identifier, Server::Configuration::FactoryContext& factory_context,
    const std::string& stat_prefix,
    Envoy::Router::RouteConfigProviderManagerImpl& route_config_provider_manager)
    : route_config_name_(rds.route_config_name()),
      scope_(factory_context.scope().createScope(stat_prefix + "rds." + route_config_name_ + ".")),
      stats_({ALL_RDS_STATS(POOL_COUNTER(*scope_))}),
      route_config_provider_manager_(route_config_provider_manager),
      manager_identifier_(manager_identifier), time_source_(factory_context.systemTimeSource()),
      last_updated_(factory_context.systemTimeSource().currentTime()) {
  ::Envoy::Config::Utility::checkLocalInfo("rds", factory_context.localInfo());

  subscription_ = Envoy::Config::SubscriptionFactory::subscriptionFromConfigSource<
      envoy::api::v2::RouteConfiguration>(
      rds.config_source(), factory_context.localInfo().node(), factory_context.dispatcher(),
      factory_context.clusterManager(), factory_context.random(), *scope_,
      [this, &rds,
       &factory_context]() -> Envoy::Config::Subscription<envoy::api::v2::RouteConfiguration>* {
        return new RdsSubscription(Envoy::Config::Utility::generateStats(*scope_), rds,
                                   factory_context.clusterManager(), factory_context.dispatcher(),
                                   factory_context.random(), factory_context.localInfo(),
                                   factory_context.scope());
      },
      "envoy.api.v2.RouteDiscoveryService.FetchRoutes",
      "envoy.api.v2.RouteDiscoveryService.StreamRoutes");
}

RdsRouteConfigSubscription::~RdsRouteConfigSubscription() {
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  runInitializeCallbackIfAny();

  // The ownership of RdsRouteConfigProviderImpl is shared among all HttpConnectionManagers that
  // hold a shared_ptr to it. The RouteConfigProviderManager holds weak_ptrs to the
  // RdsRouteConfigProviders. Therefore, the map entry for the RdsRouteConfigProvider has to get
  // cleaned by the RdsRouteConfigProvider's destructor.
  route_config_provider_manager_.route_config_subscriptions_.erase(manager_identifier_);
}

void RdsRouteConfigSubscription::onConfigUpdate(const ResourceVector& resources,
                                                const std::string& version_info) {
  last_updated_ = time_source_.currentTime();

  if (resources.empty()) {
    ENVOY_LOG(debug, "Missing RouteConfiguration for {} in onConfigUpdate()", route_config_name_);
    stats_.update_empty_.inc();
    runInitializeCallbackIfAny();
    return;
  }
  if (resources.size() != 1) {
    throw EnvoyException(fmt::format("Unexpected RDS resource length: {}", resources.size()));
  }
  const auto& route_config = resources[0];
  MessageUtil::validate(route_config);
  // TODO(PiotrSikora): Remove this hack once fixed internally.
  if (!(route_config.name() == route_config_name_)) {
    throw EnvoyException(fmt::format("Unexpected RDS configuration (expecting {}): {}",
                                     route_config_name_, route_config.name()));
  }

  const uint64_t new_hash = MessageUtil::hash(route_config);
  if (!config_info_ || new_hash != config_info_.value().last_config_hash_) {
    config_info_ = {new_hash, version_info};
    route_config_proto_ = route_config;
    stats_.config_reload_.inc();
    ENVOY_LOG(debug, "rds: loading new configuration: config_name={} hash={}", route_config_name_,
              new_hash);
    for (auto* provider : route_config_providers_) {
      provider->onConfigUpdate();
    }
  }

  runInitializeCallbackIfAny();
}

void RdsRouteConfigSubscription::onConfigUpdateFailed(const EnvoyException*) {
  // We need to allow server startup to continue, even if we have a bad
  // config.
  runInitializeCallbackIfAny();
}

void RdsRouteConfigSubscription::registerInitTarget(Init::Manager& init_manager) {
  init_manager.registerTarget(*this);
}

void RdsRouteConfigSubscription::runInitializeCallbackIfAny() {
  if (initialize_callback_) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

RdsRouteConfigProviderImpl::RdsRouteConfigProviderImpl(
    RdsRouteConfigSubscriptionSharedPtr&& subscription,
    Server::Configuration::FactoryContext& factory_context)
    : subscription_(std::move(subscription)), factory_context_(factory_context),
      tls_(factory_context.threadLocal().allocateSlot()) {
  ConfigConstSharedPtr initial_config;
  if (subscription_->config_info_.has_value()) {
    initial_config =
        std::make_shared<ConfigImpl>(subscription_->route_config_proto_, factory_context_, false);
  } else {
    initial_config = std::make_shared<NullConfigImpl>();
  }
  tls_->set([initial_config](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalConfig>(initial_config);
  });
  subscription_->route_config_providers_.insert(this);
}

RdsRouteConfigProviderImpl::~RdsRouteConfigProviderImpl() {
  subscription_->route_config_providers_.erase(this);
}

Router::ConfigConstSharedPtr RdsRouteConfigProviderImpl::config() {
  return tls_->getTyped<ThreadLocalConfig>().config_;
}

absl::optional<RouteConfigProvider::ConfigInfo> RdsRouteConfigProviderImpl::configInfo() const {
  if (!subscription_->config_info_) {
    return {};
  } else {
    return ConfigInfo{subscription_->route_config_proto_,
                      subscription_->config_info_.value().last_config_version_};
  }
}

void RdsRouteConfigProviderImpl::onConfigUpdate() {
  ConfigConstSharedPtr new_config(
      new ConfigImpl(subscription_->route_config_proto_, factory_context_, false));
  tls_->runOnAllThreads(
      [this, new_config]() -> void { tls_->getTyped<ThreadLocalConfig>().config_ = new_config; });
}

RouteConfigProviderManagerImpl::RouteConfigProviderManagerImpl(Server::Admin& admin) {
  config_tracker_entry_ =
      admin.getConfigTracker().add("routes", [this] { return dumpRouteConfigs(); });
  // ConfigTracker keys must be unique. We are asserting that no one has stolen the "routes" key
  // from us, since the returned entry will be nullptr if the key already exists.
  RELEASE_ASSERT(config_tracker_entry_, "");
}

std::vector<RouteConfigProviderSharedPtr>
RouteConfigProviderManagerImpl::getRdsRouteConfigProviders() {
  std::vector<RouteConfigProviderSharedPtr> ret;
  ret.reserve(route_config_subscriptions_.size());
  for (const auto& element : route_config_subscriptions_) {
    // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
    // in the RdsRouteConfigSubscription destructor, and the single threaded nature
    // of this code, locking the weak_ptr will not fail.
    auto subscription = element.second.lock();
    ASSERT(subscription);
    ASSERT(subscription->route_config_providers_.size() > 0);
    ret.push_back((*subscription->route_config_providers_.begin())->shared_from_this());
  }
  return ret;
};

std::vector<RouteConfigProviderSharedPtr>
RouteConfigProviderManagerImpl::getStaticRouteConfigProviders() {
  std::vector<RouteConfigProviderSharedPtr> providers_strong;
  // Collect non-expired providers.
  for (const auto& weak_provider : static_route_config_providers_) {
    const auto strong_provider = weak_provider.lock();
    if (strong_provider != nullptr) {
      providers_strong.push_back(strong_provider);
    }
  }

  // Replace our stored list of weak_ptrs with the filtered list.
  static_route_config_providers_.assign(providers_strong.begin(), providers_strong.end());

  return providers_strong;
};

Router::RouteConfigProviderSharedPtr RouteConfigProviderManagerImpl::getRdsRouteConfigProvider(
    const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
    Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix) {

  // RdsRouteConfigSubscriptions are unique based on their serialized RDS config.
  // TODO(htuch): Full serialization here gives large IDs, could get away with a
  // strong hash instead.
  const std::string manager_identifier = rds.SerializeAsString();

  RdsRouteConfigSubscriptionSharedPtr subscription;

  auto it = route_config_subscriptions_.find(manager_identifier);
  if (it == route_config_subscriptions_.end()) {
    // std::make_shared does not work for classes with private constructors. There are ways
    // around it. However, since this is not a performance critical path we err on the side
    // of simplicity.
    subscription.reset(new RdsRouteConfigSubscription(rds, manager_identifier, factory_context,
                                                      stat_prefix, *this));

    subscription->registerInitTarget(factory_context.initManager());

    route_config_subscriptions_.insert({manager_identifier, subscription});
  } else {
    // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
    // in the RdsRouteConfigSubscription destructor, and the single threaded nature
    // of this code, locking the weak_ptr will not fail.
    subscription = it->second.lock();
  }
  ASSERT(subscription);

  Router::RouteConfigProviderSharedPtr new_provider{
      new RdsRouteConfigProviderImpl(std::move(subscription), factory_context)};
  return new_provider;
};

RouteConfigProviderSharedPtr RouteConfigProviderManagerImpl::getStaticRouteConfigProvider(
    const envoy::api::v2::RouteConfiguration& route_config,
    Server::Configuration::FactoryContext& factory_context) {
  auto provider =
      std::make_shared<StaticRouteConfigProviderImpl>(std::move(route_config), factory_context);
  static_route_config_providers_.push_back(provider);
  return provider;
}

ProtobufTypes::MessagePtr RouteConfigProviderManagerImpl::dumpRouteConfigs() {
  auto config_dump = std::make_unique<envoy::admin::v2alpha::RoutesConfigDump>();

  for (const auto& provider : getRdsRouteConfigProviders()) {
    auto config_info = provider->configInfo();
    if (config_info) {
      auto* dynamic_config = config_dump->mutable_dynamic_route_configs()->Add();
      dynamic_config->set_version_info(config_info.value().version_);
      dynamic_config->mutable_route_config()->MergeFrom(config_info.value().config_);
      TimestampUtil::systemClockToTimestamp(provider->lastUpdated(),
                                            *(dynamic_config->mutable_last_updated()));
    }
  }

  for (const auto& provider : getStaticRouteConfigProviders()) {
    ASSERT(provider->configInfo());
    auto* static_config = config_dump->mutable_static_route_configs()->Add();
    static_config->mutable_route_config()->MergeFrom(provider->configInfo().value().config_);
    TimestampUtil::systemClockToTimestamp(provider->lastUpdated(),
                                          *(static_config->mutable_last_updated()));
  }

  return ProtobufTypes::MessagePtr{std::move(config_dump)};
}

} // namespace Router
} // namespace Envoy
