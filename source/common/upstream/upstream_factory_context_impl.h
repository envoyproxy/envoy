#pragma once

#include "envoy/init/manager.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Upstream {

/*
 * Upstream Factory Context used by both Clusters and Routers to configure
 * upstream HTTP filters.
 */
class UpstreamFactoryContextImpl : public Server::Configuration::UpstreamFactoryContext {
public:
  UpstreamFactoryContextImpl(Server::Configuration::ServerFactoryContext& context,
                             Init::Manager& init_manager, Stats::Scope& scope)
      : server_context_(context), init_manager_(init_manager), scope_(scope) {}

  Server::Configuration::ServerFactoryContext& serverFactoryContext() const override {
    return server_context_;
  }

  Init::Manager& initManager() override { return init_manager_; }
  Stats::Scope& scope() override { return scope_; }

private:
  Server::Configuration::ServerFactoryContext& server_context_;
  Init::Manager& init_manager_;
  Stats::Scope& scope_;
};

} // namespace Upstream
} // namespace Envoy
