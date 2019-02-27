#pragma once

#include "envoy/api/v2/srds.pb.h"
#include "envoy/router/router.h"
#include "envoy/router/scopes.h"

#include "common/router/config_impl.h"

namespace Envoy {
namespace Router {

/**
 * The implementation of scoped routing configuration logic.
 * NOTE: This is not yet implemented and simply mimics the behavior of the
 * NullScopedConfigImpl.
 *
 * TODO(AndresGuedez): implement scoped routing logic.
 */
class ScopedConfigImpl : public ScopedConfig {
public:
  ScopedConfigImpl(const envoy::api::v2::ScopedRouteConfigurationsSet& config_proto)
      : name_(config_proto.name()) {}

  Router::ConfigConstSharedPtr getRouterConfig(const Http::HeaderMap& headers) const override;

private:
  const std::string name_;
};

/**
 * An empty implementation of the scoped routing configuration.
 */
class NullScopedConfigImpl : public ScopedConfig {
public:
  Router::ConfigConstSharedPtr getRouterConfig(const Http::HeaderMap&) const override {
    return std::make_shared<const NullConfigImpl>();
  }
};

} // namespace Router
} // namespace Envoy
