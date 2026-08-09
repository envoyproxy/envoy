#include "source/common/router/static_route_provider_impl.h"

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/router/route_config_update_receiver_impl.h"
#include "source/common/router/vhds.h"

namespace Envoy {
namespace Router {

StaticRouteConfigProviderImpl::StaticRouteConfigProviderImpl(
    const envoy::config::route::v3::RouteConfiguration& config, Rds::ConfigTraits& config_traits,
    Server::Configuration::ServerFactoryContext& factory_context, Init::Manager& init_manager,
    Rds::RouteConfigProviderManager& route_config_provider_manager)
    : base_(config, config_traits, factory_context, init_manager, route_config_provider_manager),
      route_config_provider_manager_(route_config_provider_manager),
      vhds_context_(config.has_vhds()
                        ? std::make_unique<VhdsContext>(config, factory_context, init_manager,
                                                        route_config_provider_manager.protoTraits())
                        : nullptr) {}

StaticRouteConfigProviderImpl::~StaticRouteConfigProviderImpl() {
  route_config_provider_manager_.eraseStaticProvider(this);
}

Rds::ConfigConstSharedPtr StaticRouteConfigProviderImpl::config() const {
  Rds::ConfigConstSharedPtr route_config;
  if (vhds_context_ != nullptr) {
    if (auto config = vhds_context_->tls_.get(); config.has_value()) {
      route_config = config->config_;
    }
  }
  return route_config != nullptr ? route_config : base_.config();
}

const std::optional<RouteConfigProvider::ConfigInfo>&
StaticRouteConfigProviderImpl::configInfo() const {
  if (vhds_context_ == nullptr || !vhds_context_->initialized_) {
    return base_.configInfo();
  }
  return vhds_context_->config_update_info_->configInfo();
}

SystemTime StaticRouteConfigProviderImpl::lastUpdated() const {
  if (vhds_context_ == nullptr || !vhds_context_->initialized_) {
    return base_.lastUpdated();
  }
  return vhds_context_->config_update_info_->lastUpdated();
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
  // If no VHDS is configured, immediately notify the callback asynchronously that the
  // virtual host doesn't exist, shielding the filter chain stack from recursive reentrant calls.
  thread_local_dispatcher.post([current_cb = route_config_updated_cb] {
    if (auto cb = current_cb.lock()) {
      (*cb)(false);
    }
  });
}

StaticRouteConfigProviderImpl::VhdsContext::VhdsContext(
    const envoy::config::route::v3::RouteConfiguration& config,
    Server::Configuration::ServerFactoryContext& factory_context, Init::Manager& init_manager,
    Rds::ProtoTraits& proto_traits)
    : config_update_info_(
          std::make_unique<RouteConfigUpdateReceiverImpl>(proto_traits, factory_context, "")),
      factory_context_(factory_context), route_config_name_(config.name()),
      tls_(factory_context.threadLocal()),
      local_init_target_(
          fmt::format("StaticRouteConfigProvider local-init-target {}", route_config_name_),
          []() {}) {
  init_manager.add(local_init_target_);
  config_update_info_->setObserver(*this);

  // Emulate a config-update information gathering using a dynamic RouteConfigurationReceiver. This
  // is also what creates the VHDS subscription and warms up its initial fetch, so the update is
  // only published once the virtual hosts it serves have arrived.
  //
  // TODO(adisuissa): Convert the THROW_IF_NOT_OK to return an absl::StatusOr<> and propagate the
  // result through a StaticRouteConfigProviderImpl create function.
  THROW_IF_NOT_OK(config_update_info_->onRdsUpdate(config, ""));
}

void StaticRouteConfigProviderImpl::VhdsContext::onConfigWarmed() {
  // Update the worker-thread local view of the config (similar to the result of
  // RdsRouteConfigProviderImpl::onConfigUpdate()).
  Rds::ConfigConstSharedPtr parsed_config = config_update_info_->parsedConfiguration();
  tls_.set([parsed_config](Event::Dispatcher&) {
    return std::make_unique<ThreadLocalConfig>(parsed_config);
  });
  // A route configuration is live now, so whatever warms up with this provider can proceed. This is
  // what makes a listener with an inline route configuration that uses VHDS wait for the initial
  // VHDS fetch.
  local_init_target_.ready();
  initialized_ = true;

  const auto aliases = config_update_info_->resourceIdsInLastVhdsUpdate();
  // Regular (non-VHDS) updates don't populate aliases fields in resources.
  if (aliases.empty()) {
    return;
  }

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
}

void StaticRouteConfigProviderImpl::VhdsContext::requestVirtualHostsUpdate(
    const std::string& for_domain, Event::Dispatcher& thread_local_dispatcher,
    std::weak_ptr<Http::RouteConfigUpdatedCallback> route_config_updated_cb) {
  // When a request for a VHDS update happens, Envoy needs to send the alias as
  // the resource name.
  auto alias = VhdsSubscription::domainNameToAlias(route_config_name_, for_domain);
  // The StaticRouteConfigProviderImpl instance can go away before the dispatcher has a chance to
  // execute the callback. still_alive shared_ptr will be deallocated when the current instance of
  // the StaticRouteConfigProviderImpl is deallocated; we rely on a weak_ptr to still_alive flag to
  // determine if the StaticRouteConfigProviderImpl instance is still valid.
  factory_context_.mainThreadDispatcher().post(
      [this, maybe_still_alive = std::weak_ptr<bool>(still_alive_), alias, &thread_local_dispatcher,
       route_config_updated_cb]() -> void {
        if (maybe_still_alive.lock()) {
          config_update_info_->updateOnDemand(alias);
          config_update_callbacks_.push_back(
              {alias, thread_local_dispatcher, route_config_updated_cb});
        }
      });
}

} // namespace Router
} // namespace Envoy
