#pragma once

#include "envoy/server/factory_context.h"

#include "source/common/common/assert.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

/**
 * Factory context that is used to create the HTTP filters of the HTTP connection manager. Every
 * method of the factory context of the network filter chain is delegated to it unchanged; the only
 * thing this adds is statsPrefixScope(), the 'http.<stat_prefix>.' scope of the connection manager.
 *
 * The connection manager uses this to hand its filters a scope that their stats prefix has already
 * been applied to, so that they no longer prepend that prefix to their stat names themselves.
 * Server::Configuration::ExtraFactoryContext::create() recognizes this context and exposes the
 * scope through statsPrefixScopeOr(); see envoy/server/filter_config.h.
 *
 * NOTE: statsPrefixScope() is deliberately not prefixedScope(). prefixedScope() is the scope of the
 * listener ('listener.<address>.') for every factory context including this one, and has nothing to
 * do with the stat prefix of a filter. The two must not be conflated, and this context leaves
 * prefixedScope() alone so that the filters that read it keep seeing the listener scope.
 *
 * The scope only stands in for one specific stats prefix, the one it was created from, which is
 * why that prefix is carried alongside it: a filter that is created with a different prefix, such
 * as an ECDS filter with its 'extension_config_discovery.http_filter.<name>.' prefix, must not be
 * given this scope, or its stats would both lose their own prefix and move under this one.
 */
class HttpFilterFactoryContext : public Server::Configuration::FactoryContext {
public:
  /**
   * @param context the factory context of the network filter chain that everything is delegated to.
   * @param stats_prefix_scope the scope named after stats_prefix.
   * @param stats_prefix the stats prefix that stats_prefix_scope stands in for. It is copied, so it
   *        need not outlive this context.
   */
  HttpFilterFactoryContext(Server::Configuration::FactoryContext& context,
                           Stats::ScopeSharedPtr stats_prefix_scope, absl::string_view stats_prefix)
      : context_(context), stats_prefix_scope_(std::move(stats_prefix_scope)),
        stats_prefix_(stats_prefix) {
    ASSERT(stats_prefix_scope_ != nullptr);
  }

  /**
   * @return the scope that stats_prefix has already been applied to, that is the
   *         'http.<stat_prefix>.' scope of the connection manager.
   */
  Stats::Scope& statsPrefixScope() const { return *stats_prefix_scope_; }

  /**
   * @return the stats prefix that statsPrefixScope() stands in for. Only the filters that are
   *         created with this very prefix may be given that scope.
   */
  absl::string_view statsPrefix() const { return stats_prefix_; }

  // Server::Configuration::GenericFactoryContext
  Server::Configuration::ServerFactoryContext& serverFactoryContext() override {
    return context_.serverFactoryContext();
  }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return context_.messageValidationVisitor();
  }
  Init::Manager& initManager() override { return context_.initManager(); }
  Stats::Scope& scope() override { return context_.scope(); }

  // Server::Configuration::FactoryContext
  const Network::DrainDecision& drainDecision() override { return context_.drainDecision(); }
  envoy::config::core::v3::TrafficDirection direction() const override {
    return context_.direction();
  }
  bool isQuic() const override { return context_.isQuic(); }
  bool shouldBypassOverloadManager() const override {
    return context_.shouldBypassOverloadManager();
  }
  Stats::Scope& prefixedScope() override { return context_.prefixedScope(); }

private:
  Server::Configuration::FactoryContext& context_;
  const Stats::ScopeSharedPtr stats_prefix_scope_;
  const std::string stats_prefix_;
};

using HttpFilterFactoryContextPtr = std::unique_ptr<HttpFilterFactoryContext>;

} // namespace Http
} // namespace Envoy
