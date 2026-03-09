#include "source/common/router/static_route_provider_impl.h"

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/router/route_config_update_receiver_impl.h"

namespace Envoy {
namespace Router {

StaticRouteConfigProviderImpl::StaticRouteConfigProviderImpl(
    const envoy::config::route::v3::RouteConfiguration& config, Rds::ConfigTraits& config_traits,
    Server::Configuration::ServerFactoryContext& factory_context,
    Rds::RouteConfigProviderManager& route_config_provider_manager)
    : base_(config, config_traits, factory_context, route_config_provider_manager),
      route_config_provider_manager_(route_config_provider_manager) {
  if (config.has_vhds()) {
    vhds_context_ = std::make_unique<VhdsContext>(config, factory_context, *this,
                                                  route_config_provider_manager);
  }
}

StaticRouteConfigProviderImpl::~StaticRouteConfigProviderImpl() {
  route_config_provider_manager_.eraseStaticProvider(this);
}

Rds::ConfigConstSharedPtr StaticRouteConfigProviderImpl::config() const {
  return vhds_context_ ? vhds_context_->tls_->config_ : base_.config();
}

const absl::optional<RouteConfigProvider::ConfigInfo>&
StaticRouteConfigProviderImpl::configInfo() const {
  if (vhds_context_) {
    return vhds_context_->config_update_info_->configInfo();
  }
  return base_.configInfo();
}

SystemTime StaticRouteConfigProviderImpl::lastUpdated() const {
  if (vhds_context_) {
    return vhds_context_->config_update_info_->lastUpdated();
  }
  return base_.lastUpdated();
}

absl::Status StaticRouteConfigProviderImpl::onConfigUpdate() {
  if (vhds_context_) {
    return vhds_context_->onConfigUpdate();
  }
  return absl::OkStatus();
}

ConfigConstSharedPtr StaticRouteConfigProviderImpl::configCast() const {
  // This should always be the case for StaticRouteConfigProviderImpl's config.
  ASSERT(dynamic_cast<const Config*>(StaticRouteConfigProviderImpl::config().get()));
  return std::static_pointer_cast<const Config>(StaticRouteConfigProviderImpl::config());
}

void StaticRouteConfigProviderImpl::requestVirtualHostsUpdate(
    const std::string& for_domain, Event::Dispatcher& thread_local_dispatcher,
    std::weak_ptr<Http::RouteConfigUpdatedCallback> route_config_updated_cb) {
  if (vhds_context_) {
    vhds_context_->requestVirtualHostsUpdate(for_domain, thread_local_dispatcher,
                                             route_config_updated_cb);
    return;
  }
  // If no VHDS is configured, immediately notify the callback that the
  // virtual-host doesn't exist.
  auto current_cb = route_config_updated_cb.lock();
  if (current_cb) {
    thread_local_dispatcher.post([current_cb] { (*current_cb)(false); });
  }
}

StaticRouteConfigProviderImpl::VhdsContext::VhdsContext(
    const envoy::config::route::v3::RouteConfiguration& config,
    Server::Configuration::ServerFactoryContext& factory_context,
    StaticRouteConfigProviderImpl& parent,
    Rds::RouteConfigProviderManager& route_config_provider_manager)
    : factory_context_(factory_context), tls_(factory_context.threadLocal()) {
  // Emulate a config-update information gathering using a dynamic RouteConfigurationReceiver.
  config_update_info_ = std::make_unique<RouteConfigUpdateReceiverImpl>(
      route_config_provider_manager.protoTraits(), factory_context);
  config_update_info_->onRdsUpdate(config, "");
  vhds_subscription_ = THROW_OR_RETURN_VALUE(
      VhdsSubscription::createVhdsSubscription(config_update_info_, factory_context, "", &parent),
      VhdsSubscriptionPtr);
  vhds_subscription_->registerInitTargetWithInitManager(factory_context.initManager());
  tls_.set([initial_config = parent.base_.config()](Event::Dispatcher&) {
    return std::make_unique<ThreadLocalConfig>(initial_config);
  });
}

absl::Status StaticRouteConfigProviderImpl::VhdsContext::onConfigUpdate() {
  const auto aliases = config_update_info_->resourceIdsInLastVhdsUpdate();
  // Regular (non-VHDS) updates don't populate aliases fields in resources.
  if (aliases.empty()) {
    return absl::OkStatus();
  }

  // Update the worker-thread local view of the config (similar to the result of
  // RdsRouteConfigProviderImpl::onConfigUpdate()).
  Rds::ConfigConstSharedPtr parsed_config = config_update_info_->parsedConfiguration();
  tls_.set([parsed_config](Event::Dispatcher&) {
    return std::make_unique<ThreadLocalConfig>(parsed_config);
  });

  const auto config = std::static_pointer_cast<const ConfigImpl>(parsed_config);
  // Notifies connections that RouteConfiguration update has been propagated.
  // Callbacks processing is performed in FIFO order. The callback is skipped if alias used in
  // the VHDS update request do not match the aliases in the update response
  for (auto it = config_update_callbacks_.begin(); it != config_update_callbacks_.end();) {
    auto found = aliases.find(it->alias_);
    if (found != aliases.end()) {
      auto host_header = Http::RequestHeaderMapImpl::create();
      host_header->setHost(VhdsSubscription::aliasToDomainName(it->alias_));
      const bool host_exists = config->virtualHostExists(*host_header);
      std::weak_ptr<Http::RouteConfigUpdatedCallback> current_cb(it->cb_);
      it->thread_local_dispatcher_.post([current_cb, host_exists] {
        if (auto cb = current_cb.lock()) {
          (*cb)(host_exists);
        }
      });
      it = config_update_callbacks_.erase(it);
    } else {
      it++;
    }
  }
  return absl::OkStatus();
}

void StaticRouteConfigProviderImpl::VhdsContext::requestVirtualHostsUpdate(
    const std::string& for_domain, Event::Dispatcher& thread_local_dispatcher,
    std::weak_ptr<Http::RouteConfigUpdatedCallback> route_config_updated_cb) {
  // When a request for a VHDS update happens, Envoy needs to send the alias as
  // the resource name.
  auto alias = VhdsSubscription::domainNameToAlias(
      config_update_info_->protobufConfigurationCast().name(), for_domain);
  // The StaticRouteConfigProviderImpl instance can go away before the dispatcher has a chance to
  // execute the callback. still_alive shared_ptr will be deallocated when the current instance of
  // the RdsRouteConfigProviderImpl is deallocated; we rely on a weak_ptr to still_alive flag to
  // determine if the RdsRouteConfigProviderImpl instance is still valid.
  factory_context_.mainThreadDispatcher().post(
      [this, maybe_still_alive = std::weak_ptr<bool>(still_alive_), alias, &thread_local_dispatcher,
       route_config_updated_cb]() -> void {
        if (maybe_still_alive.lock()) {
          vhds_subscription_->updateOnDemand(alias);
          config_update_callbacks_.push_back(
              {alias, thread_local_dispatcher, route_config_updated_cb});
        }
      });
}

} // namespace Router
} // namespace Envoy
