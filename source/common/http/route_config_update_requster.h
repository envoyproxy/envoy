#pragma once

#include "envoy/http/filter.h"

#include "source/common/router/rds_impl.h"
#include "source/common/router/scoped_rds.h"

namespace Envoy {
namespace Http {

class RdsRouteConfigUpdateRequester : public RouteConfigUpdateRequester {
public:
  RdsRouteConfigUpdateRequester(Router::RouteConfigProvider* route_config_provider)
      : route_config_provider_(route_config_provider) {}

  RdsRouteConfigUpdateRequester(Config::ConfigProvider* scoped_route_config_provider,
                                OptRef<const Router::ScopeKeyBuilder> scope_key_builder)
      // Expect the dynamic cast to succeed because only ScopedRdsConfigProvider is fully
      // implemented. Inline provider will be cast to nullptr here but it is not full implemented
      // and can't not be used at this point. Should change this implementation if we have a
      // functional inline scope route provider in the future.
      : scoped_route_config_provider_(
            dynamic_cast<Router::ScopedRdsConfigProvider*>(scoped_route_config_provider)),
        scope_key_builder_(scope_key_builder) {}

  void requestRouteConfigUpdate(RouteCache& route_cache,
                                Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb,
                                absl::optional<Router::ConfigConstSharedPtr> route_config,
                                Event::Dispatcher& dispatcher,
                                RequestHeaderMap& request_headers) override;
  void
  requestVhdsUpdate(const std::string& host_header, Event::Dispatcher& thread_local_dispatcher,
                    Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb) override;
  void
  requestSrdsUpdate(RouteCache& route_cache, Router::ScopeKeyPtr scope_key,
                    Event::Dispatcher& thread_local_dispatcher,
                    Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb) override;

private:
  Router::RouteConfigProvider* route_config_provider_;
  Router::ScopedRdsConfigProvider* scoped_route_config_provider_;
  OptRef<const Router::ScopeKeyBuilder> scope_key_builder_;
};

class RdsRouteConfigUpdateRequesterFactory : public RouteConfigUpdateRequesterFactory {
public:
  // UntypedFactory
  virtual std::string name() const override {
    return "envoy.route_config_update_requester.default";
  }

  std::unique_ptr<RouteConfigUpdateRequester>
  createRouteConfigUpdateRequester(Router::RouteConfigProvider* route_config_provider) override {
    return std::make_unique<RdsRouteConfigUpdateRequester>(route_config_provider);
  }
  std::unique_ptr<RouteConfigUpdateRequester> createRouteConfigUpdateRequester(
      Config::ConfigProvider* scoped_route_config_provider,
      OptRef<const Router::ScopeKeyBuilder> scope_key_builder) override {
    return std::make_unique<RdsRouteConfigUpdateRequester>(scoped_route_config_provider,
                                                           scope_key_builder);
  }
};

DECLARE_FACTORY(RdsRouteConfigUpdateRequesterFactory);

} // namespace Http
} // namespace Envoy
