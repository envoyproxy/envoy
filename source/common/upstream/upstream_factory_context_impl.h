#pragma once

#include <string>

#include "envoy/common/optref.h"
#include "envoy/init/manager.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Upstream {

/*
 * Upstream Factory Context used by both Clusters and Routers to configure
 * upstream HTTP filters.
 */
class UpstreamFactoryContextImpl : public Server::Configuration::UpstreamFactoryContext {
public:
  UpstreamFactoryContextImpl(Server::Configuration::ServerFactoryContext& context,
                             Init::Manager& init_manager, Stats::Scope& scope,
                             OptRef<Stats::Scope> stats_prefix_scope = {},
                             absl::string_view stats_prefix = {})
      : server_context_(context), init_manager_(init_manager), scope_(scope),
        stats_prefix_scope_(stats_prefix_scope), stats_prefix_(stats_prefix) {}

  Server::Configuration::ServerFactoryContext& serverFactoryContext() override {
    return server_context_;
  }

  Init::Manager& initManager() override { return init_manager_; }
  Stats::Scope& scope() override { return scope_; }
  OptRef<Stats::Scope> statsPrefixScope() override { return stats_prefix_scope_; }
  absl::string_view statsPrefix() const override { return stats_prefix_; }

private:
  Server::Configuration::ServerFactoryContext& server_context_;
  Init::Manager& init_manager_;
  Stats::Scope& scope_;
  const OptRef<Stats::Scope> stats_prefix_scope_;
  const std::string stats_prefix_;
};

} // namespace Upstream
} // namespace Envoy
