#include "common/router/rds_impl.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/api/v2/discovery.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/rds.pb.validate.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/service/route/v3alpha/rds.pb.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/api_version.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"
#include "common/router/config_impl.h"

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
    return route_config_provider_manager.createStaticRouteConfigProvider(config.route_config(),
                                                                         factory_context);
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::kRds:
    return route_config_provider_manager.createRdsRouteConfigProvider(
        config.rds(), factory_context, stat_prefix, factory_context.initManager());
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

StaticRouteConfigProviderImpl::StaticRouteConfigProviderImpl(
    const envoy::api::v2::RouteConfiguration& config,
    Server::Configuration::FactoryContext& factory_context,
    RouteConfigProviderManagerImpl& route_config_provider_manager)
    : config_(new ConfigImpl(config, factory_context.getServerFactoryContext(),
                             factory_context.messageValidationVisitor(), true)),

      route_config_proto_{config}, last_updated_(factory_context.timeSource().systemTime()),
      route_config_provider_manager_(route_config_provider_manager) {
  route_config_provider_manager_.static_route_config_providers_.insert(this);
}

StaticRouteConfigProviderImpl::~StaticRouteConfigProviderImpl() {
  route_config_provider_manager_.static_route_config_providers_.erase(this);
}

// TODO(htuch): If support for multiple clusters is added per #1170 cluster_name_
RdsRouteConfigSubscription::RdsRouteConfigSubscription(
    const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
    const uint64_t manager_identifier, Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator, Init::Manager& init_manager,
    const std::string& stat_prefix,
    Envoy::Router::RouteConfigProviderManagerImpl& route_config_provider_manager,
    envoy::api::v2::core::ConfigSource::XdsApiVersion xds_api_version)
    : route_config_name_(rds.route_config_name()), factory_context_(factory_context),
      validator_(validator), init_manager_(init_manager),
      init_target_(fmt::format("RdsRouteConfigSubscription {}", route_config_name_),
                   [this]() { subscription_->start({route_config_name_}); }),
      scope_(factory_context.scope().createScope(stat_prefix + "rds." + route_config_name_ + ".")),
      stat_prefix_(stat_prefix), stats_({ALL_RDS_STATS(POOL_COUNTER(*scope_))}),
      route_config_provider_manager_(route_config_provider_manager),
      manager_identifier_(manager_identifier), xds_api_version_(xds_api_version) {

  subscription_ =
      factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          rds.config_source(), loadTypeUrl(), *scope_, *this);
  config_update_info_ =
      std::make_unique<RouteConfigUpdateReceiverImpl>(factory_context.timeSource(), validator);
}

RdsRouteConfigSubscription::~RdsRouteConfigSubscription() {
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  init_target_.ready();

  // The ownership of RdsRouteConfigProviderImpl is shared among all HttpConnectionManagers that
  // hold a shared_ptr to it. The RouteConfigProviderManager holds weak_ptrs to the
  // RdsRouteConfigProviders. Therefore, the map entry for the RdsRouteConfigProvider has to get
  // cleaned by the RdsRouteConfigProvider's destructor.
  route_config_provider_manager_.dynamic_route_config_providers_.erase(manager_identifier_);
}

void RdsRouteConfigSubscription::onConfigUpdate(
    const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
    const std::string& version_info) {
  if (!validateUpdateSize(resources.size())) {
    return;
  }
  auto route_config = MessageUtil::anyConvert<envoy::api::v2::RouteConfiguration>(resources[0]);
  MessageUtil::validate(route_config, validator_);
  if (route_config.name() != route_config_name_) {
    throw EnvoyException(fmt::format("Unexpected RDS configuration (expecting {}): {}",
                                     route_config_name_, route_config.name()));
  }
  for (auto* provider : route_config_providers_) {
    // This seems inefficient, though it is necessary to validate config in each context,
    // especially when it comes with per_filter_config,
    provider->validateConfig(route_config);
  }

  std::unique_ptr<Init::ManagerImpl> noop_init_manager;
  std::unique_ptr<Cleanup> resume_rds;
  if (config_update_info_->onRdsUpdate(route_config, version_info)) {
    stats_.config_reload_.inc();

    if (config_update_info_->routeConfiguration().has_vhds()) {
      ENVOY_LOG(debug, "rds: vhds configuration present, starting vhds: config_name={} hash={}",
                route_config_name_, config_update_info_->configHash());
      maybeCreateInitManager(version_info, noop_init_manager, resume_rds);
      // TODO(dmitri-d): It's unsafe to depend directly on factory context here,
      // the listener might have been torn down, need to remove this.
      vhds_subscription_ = std::make_unique<VhdsSubscription>(
          config_update_info_, factory_context_, stat_prefix_, route_config_providers_,
          config_update_info_->routeConfiguration().vhds().config_source().xds_api_version());
      vhds_subscription_->registerInitTargetWithInitManager(
          noop_init_manager == nullptr ? getRdsConfigInitManager() : *noop_init_manager);
    } else {
      ENVOY_LOG(debug, "rds: loading new configuration: config_name={} hash={}", route_config_name_,
                config_update_info_->configHash());

      for (auto* provider : route_config_providers_) {
        provider->onConfigUpdate();
      }
      vhds_subscription_.release();
    }
    update_callback_manager_.runCallbacks();
  }

  init_target_.ready();
}

// Initialize a no-op InitManager in case the one in the factory_context has completed
// initialization. This can happen if an RDS config update for an already established RDS
// subscription contains VHDS configuration.
void RdsRouteConfigSubscription::maybeCreateInitManager(
    const std::string& version_info, std::unique_ptr<Init::ManagerImpl>& init_manager,
    std::unique_ptr<Cleanup>& init_vhds) {
  if (getRdsConfigInitManager().state() == Init::Manager::State::Initialized) {
    init_manager = std::make_unique<Init::ManagerImpl>(
        fmt::format("VHDS {}:{}", route_config_name_, version_info));
    init_vhds = std::make_unique<Cleanup>([this, &init_manager, version_info] {
      // For new RDS subscriptions created after listener warming up, we don't wait for them to warm
      // up.
      Init::WatcherImpl noop_watcher(
          // Note: we just throw it away.
          fmt::format("VHDS ConfigUpdate watcher {}:{}", route_config_name_, version_info),
          []() { /*Do nothing.*/ });
      init_manager->initialize(noop_watcher);
    });
  }
}

void RdsRouteConfigSubscription::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, const std::string&) {
  if (!removed_resources.empty()) {
    // TODO(#2500) when on-demand resource loading is supported, an RDS removal may make sense (see
    // discussion in #6879), and so we should do something other than ignoring here.
    ENVOY_LOG(
        error,
        "Server sent a delta RDS update attempting to remove a resource (name: {}). Ignoring.",
        removed_resources[0]);
  }
  if (!added_resources.empty()) {
    Protobuf::RepeatedPtrField<ProtobufWkt::Any> unwrapped_resource;
    *unwrapped_resource.Add() = added_resources[0].resource();
    onConfigUpdate(unwrapped_resource, added_resources[0].version());
  }
}

void RdsRouteConfigSubscription::onConfigUpdateFailed(
    Envoy::Config::ConfigUpdateFailureReason reason, const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad
  // config.
  init_target_.ready();
}

bool RdsRouteConfigSubscription::validateUpdateSize(int num_resources) {
  if (num_resources == 0) {
    ENVOY_LOG(debug, "Missing RouteConfiguration for {} in onConfigUpdate()", route_config_name_);
    stats_.update_empty_.inc();
    init_target_.ready();
    return false;
  }
  if (num_resources != 1) {
    throw EnvoyException(fmt::format("Unexpected RDS resource length: {}", num_resources));
    // (would be a return false here)
  }
  return true;
}

std::string RdsRouteConfigSubscription::loadTypeUrl() {
  switch (xds_api_version_) {
  // automatically set api version as V2
  case envoy::api::v2::core::ConfigSource::AUTO:
  case envoy::api::v2::core::ConfigSource::V2:
    return Grpc::Common::typeUrl(
        API_NO_BOOST(envoy::api::v2::RouteConfiguration().GetDescriptor()->full_name()));
  case envoy::api::v2::core::ConfigSource::V3ALPHA:
    return Grpc::Common::typeUrl(API_NO_BOOST(
        envoy::service::route::v3alpha::RouteConfiguration().GetDescriptor()->full_name()));
  default:
    throw EnvoyException(fmt::format("type {} is not supported", xds_api_version_));
  }
}

RdsRouteConfigProviderImpl::RdsRouteConfigProviderImpl(
    RdsRouteConfigSubscriptionSharedPtr&& subscription,
    Server::Configuration::FactoryContext& factory_context)
    : subscription_(std::move(subscription)),
      config_update_info_(subscription_->routeConfigUpdate()),
      factory_context_(factory_context.getServerFactoryContext()),
      validator_(factory_context.messageValidationVisitor()),
      tls_(factory_context.threadLocal().allocateSlot()) {
  ConfigConstSharedPtr initial_config;
  if (config_update_info_->configInfo().has_value()) {
    initial_config = std::make_shared<ConfigImpl>(config_update_info_->routeConfiguration(),
                                                  factory_context_, validator_, false);
  } else {
    initial_config = std::make_shared<NullConfigImpl>();
  }
  tls_->set([initial_config](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalConfig>(initial_config);
  });
  // It should be 1:1 mapping due to shared rds config.
  ASSERT(subscription_->routeConfigProviders().empty());
  subscription_->routeConfigProviders().insert(this);
}

RdsRouteConfigProviderImpl::~RdsRouteConfigProviderImpl() {
  subscription_->routeConfigProviders().erase(this);
  // It should be 1:1 mapping due to shared rds config.
  ASSERT(subscription_->routeConfigProviders().empty());
}

Router::ConfigConstSharedPtr RdsRouteConfigProviderImpl::config() {
  return tls_->getTyped<ThreadLocalConfig>().config_;
}

void RdsRouteConfigProviderImpl::onConfigUpdate() {
  ConfigConstSharedPtr new_config(new ConfigImpl(config_update_info_->routeConfiguration(),
                                                 factory_context_, validator_, false));
  tls_->runOnAllThreads([new_config](ThreadLocal::ThreadLocalObjectSharedPtr previous)
                            -> ThreadLocal::ThreadLocalObjectSharedPtr {
    auto prev_config = std::dynamic_pointer_cast<ThreadLocalConfig>(previous);
    prev_config->config_ = new_config;
    return previous;
  });
}

void RdsRouteConfigProviderImpl::validateConfig(
    const envoy::api::v2::RouteConfiguration& config) const {
  // TODO(lizan): consider cache the config here until onConfigUpdate.
  ConfigImpl validation_config(config, factory_context_, validator_, false);
}

RouteConfigProviderManagerImpl::RouteConfigProviderManagerImpl(Server::Admin& admin) {
  config_tracker_entry_ =
      admin.getConfigTracker().add("routes", [this] { return dumpRouteConfigs(); });
  // ConfigTracker keys must be unique. We are asserting that no one has stolen the "routes" key
  // from us, since the returned entry will be nullptr if the key already exists.
  RELEASE_ASSERT(config_tracker_entry_, "");
}

Router::RouteConfigProviderSharedPtr RouteConfigProviderManagerImpl::createRdsRouteConfigProvider(
    const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
    Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix,
    Init::Manager& init_manager) {
  // RdsRouteConfigSubscriptions are unique based on their serialized RDS config.
  const uint64_t manager_identifier = MessageUtil::hash(rds);
  auto it = dynamic_route_config_providers_.find(manager_identifier);

  if (it == dynamic_route_config_providers_.end()) {
    // std::make_shared does not work for classes with private constructors. There are ways
    // around it. However, since this is not a performance critical path we err on the side
    // of simplicity.
    RdsRouteConfigSubscriptionSharedPtr subscription(new RdsRouteConfigSubscription(
        rds, manager_identifier, factory_context.getServerFactoryContext(),
        factory_context.messageValidationVisitor(), factory_context.initManager(), stat_prefix,
        *this, rds.config_source().xds_api_version()));
    init_manager.add(subscription->init_target_);
    std::shared_ptr<RdsRouteConfigProviderImpl> new_provider{
        new RdsRouteConfigProviderImpl(std::move(subscription), factory_context)};
    dynamic_route_config_providers_.insert(
        {manager_identifier, std::weak_ptr<RdsRouteConfigProviderImpl>(new_provider)});
    return new_provider;
  } else {
    // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
    // in the RdsRouteConfigSubscription destructor, and the single threaded nature
    // of this code, locking the weak_ptr will not fail.
    auto existing_provider = it->second.lock();
    RELEASE_ASSERT(existing_provider != nullptr,
                   absl::StrCat("cannot find subscribed rds resource ", rds.route_config_name()));
    init_manager.add(existing_provider->subscription_->init_target_);
    return existing_provider;
  }
}

RouteConfigProviderPtr RouteConfigProviderManagerImpl::createStaticRouteConfigProvider(
    const envoy::api::v2::RouteConfiguration& route_config,
    Server::Configuration::FactoryContext& factory_context) {
  auto provider =
      std::make_unique<StaticRouteConfigProviderImpl>(route_config, factory_context, *this);
  static_route_config_providers_.insert(provider.get());
  return provider;
}

std::unique_ptr<envoy::admin::v2alpha::RoutesConfigDump>
RouteConfigProviderManagerImpl::dumpRouteConfigs() const {
  auto config_dump = std::make_unique<envoy::admin::v2alpha::RoutesConfigDump>();

  for (const auto& element : dynamic_route_config_providers_) {
    const auto& subscription = element.second.lock()->subscription_;
    // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
    // in the RdsRouteConfigSubscription destructor, and the single threaded nature
    // of this code, locking the weak_ptr will not fail.
    ASSERT(subscription);
    ASSERT(!subscription->route_config_providers_.empty());

    if (subscription->routeConfigUpdate()->configInfo()) {
      auto* dynamic_config = config_dump->mutable_dynamic_route_configs()->Add();
      dynamic_config->set_version_info(subscription->routeConfigUpdate()->configVersion());
      dynamic_config->mutable_route_config()->MergeFrom(
          subscription->routeConfigUpdate()->routeConfiguration());
      TimestampUtil::systemClockToTimestamp(subscription->routeConfigUpdate()->lastUpdated(),
                                            *dynamic_config->mutable_last_updated());
    }
  }

  for (const auto& provider : static_route_config_providers_) {
    ASSERT(provider->configInfo());
    auto* static_config = config_dump->mutable_static_route_configs()->Add();
    static_config->mutable_route_config()->MergeFrom(provider->configInfo().value().config_);
    TimestampUtil::systemClockToTimestamp(provider->lastUpdated(),
                                          *static_config->mutable_last_updated());
  }

  return config_dump;
}

} // namespace Router
} // namespace Envoy
