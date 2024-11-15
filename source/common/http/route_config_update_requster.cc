#include "source/common/http/route_config_update_requster.h"

namespace Envoy {
namespace Http {

// TODO(chaoqin-li1123): Make on demand vhds and on demand srds works at the same time.
void RdsRouteConfigUpdateRequester::requestRouteConfigUpdate(
    RouteCache& route_cache, Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb,
    absl::optional<Router::ConfigConstSharedPtr> route_config, Event::Dispatcher& dispatcher,
    RequestHeaderMap& request_headers) {
  if (route_config.has_value() && route_config.value()->usesVhds()) {
    ASSERT(!request_headers.Host()->value().empty());
    const auto& host_header = absl::AsciiStrToLower(request_headers.getHostValue());
    requestVhdsUpdate(host_header, dispatcher, std::move(route_config_updated_cb));
    return;
  } else if (scope_key_builder_.has_value()) {
    Router::ScopeKeyPtr scope_key = scope_key_builder_->computeScopeKey(request_headers);
    // If scope_key is not null, the scope exists but RouteConfiguration is not initialized.
    if (scope_key != nullptr) {
      requestSrdsUpdate(route_cache, std::move(scope_key), dispatcher,
                        std::move(route_config_updated_cb));
      return;
    }
  }
  // Continue the filter chain if no on demand update is requested.
  (*route_config_updated_cb)(false);
}

void RdsRouteConfigUpdateRequester::requestVhdsUpdate(
    const std::string& host_header, Event::Dispatcher& thread_local_dispatcher,
    Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb) {
  route_config_provider_->requestVirtualHostsUpdate(host_header, thread_local_dispatcher,
                                                    std::move(route_config_updated_cb));
}

void RdsRouteConfigUpdateRequester::requestSrdsUpdate(
    RouteCache& route_cache, Router::ScopeKeyPtr scope_key,
    Event::Dispatcher& thread_local_dispatcher,
    Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb) {
  // Since inline scope_route_config_provider is not fully implemented and never used,
  // dynamic cast in constructor always succeed and the pointer should not be null here.
  ASSERT(scoped_route_config_provider_ != nullptr);
  Http::RouteConfigUpdatedCallback scoped_route_config_updated_cb =
      Http::RouteConfigUpdatedCallback(
          [weak_route_config_updated_cb =
               std::weak_ptr<Http::RouteConfigUpdatedCallback>(route_config_updated_cb),
           &route_cache](bool scope_exist) {
            // If the callback can be locked, this ActiveStream is still alive.
            if (auto cb = weak_route_config_updated_cb.lock()) {
              // Refresh the route before continue the filter chain.
              if (scope_exist) {
                route_cache.refreshCachedRoute();
              }
              (*cb)(scope_exist && route_cache.hasCachedRoute());
            }
          });
  scoped_route_config_provider_->onDemandRdsUpdate(std::move(scope_key), thread_local_dispatcher,
                                                   std::move(scoped_route_config_updated_cb));
}

REGISTER_FACTORY(RdsRouteConfigUpdateRequesterFactory, RouteConfigUpdateRequesterFactory);

} // namespace Http
} // namespace Envoy
