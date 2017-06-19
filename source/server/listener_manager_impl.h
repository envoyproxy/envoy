#pragma once

#include "envoy/server/filter_config.h"
#include "envoy/server/listener_manager.h"

#include "common/common/logger.h"
#include "common/json/json_validator.h"

namespace Envoy {
namespace Server {

/**
 * Prod implementation of ListenSocketFactory that creates real sockets and attempts to fetch
 * sockets from the parent process via the hot restarter.
 */
class ProdListenSocketFactory : public ListenSocketFactory, Logger::Loggable<Logger::Id::config> {
public:
  ProdListenSocketFactory(HotRestart& restarter) : restarter_(restarter) {}

  // Server::ListenSocketFactory
  Network::ListenSocketPtr create(Network::Address::InstanceConstSharedPtr address,
                                  bool bind_to_port) override;

private:
  HotRestart& restarter_;
};

/**
 * Maps JSON config to runtime config for a listener with a network filter chain.
 */
class ListenerImpl : public Listener,
                     public Configuration::FactoryContext,
                     public Network::FilterChainFactory,
                     Json::Validator,
                     Logger::Loggable<Logger::Id::config> {
public:
  ListenerImpl(Instance& server, ListenSocketFactory& factory, const Json::Object& json);

  // Server::Listener
  Network::FilterChainFactory& filterChainFactory() override { return *this; }
  Network::Address::InstanceConstSharedPtr address() override { return address_; }
  Network::ListenSocket& socket() override { return *socket_; }
  bool bindToPort() override { return bind_to_port_; }
  Ssl::ServerContext* sslContext() override { return ssl_context_.get(); }
  bool useProxyProto() override { return use_proxy_proto_; }
  bool useOriginalDst() override { return use_original_dst_; }
  uint32_t perConnectionBufferLimitBytes() override { return per_connection_buffer_limit_bytes_; }
  Stats::Scope& listenerScope() override { return *listener_scope_; }

  // Server::Configuration::FactoryContext
  AccessLog::AccessLogManager& accessLogManager() override { return server_.accessLogManager(); }
  Upstream::ClusterManager& clusterManager() override { return server_.clusterManager(); }
  Event::Dispatcher& dispatcher() override { return server_.dispatcher(); }
  DrainManager& drainManager() override { return server_.drainManager(); }
  bool healthCheckFailed() override { return server_.healthCheckFailed(); }
  Tracing::HttpTracer& httpTracer() override { return server_.httpTracer(); }
  Init::Manager& initManager() override { return server_.initManager(); }
  const LocalInfo::LocalInfo& localInfo() override { return server_.localInfo(); }
  Envoy::Runtime::RandomGenerator& random() override { return server_.random(); }
  RateLimit::ClientPtr
  rateLimitClient(const Optional<std::chrono::milliseconds>& timeout) override {
    return server_.rateLimitClient(timeout);
  }
  Envoy::Runtime::Loader& runtime() override { return server_.runtime(); }
  Instance& server() override { return server_; }
  Stats::Scope& scope() override { return *global_scope_; }
  ThreadLocal::Instance& threadLocal() override { return server_.threadLocal(); }

  // Network::FilterChainFactory
  bool createFilterChain(Network::Connection& connection) override;

private:
  Instance& server_;
  Network::Address::InstanceConstSharedPtr address_;
  Network::ListenSocketPtr socket_;
  Stats::ScopePtr global_scope_;   // Stats with global named scope, but needed for LDS cleanup.
  Stats::ScopePtr listener_scope_; // Stats with listener named scope.
  Ssl::ServerContextPtr ssl_context_;
  const bool bind_to_port_{};
  const bool use_proxy_proto_{};
  const bool use_original_dst_{};
  const uint32_t per_connection_buffer_limit_bytes_{};
  std::list<Configuration::NetworkFilterFactoryCb> filter_factories_;
};

/**
 * Implementation of ListenerManager.
 */
class ListenerManagerImpl : public ListenerManager {
public:
  ListenerManagerImpl(Instance& server, ListenSocketFactory& factory)
      : server_(server), factory_(factory) {}

  // Server::ListenerManager
  void addListener(const Json::Object& json) override;
  std::list<std::reference_wrapper<Listener>> listeners() override;

private:
  Instance& server_;
  ListenSocketFactory& factory_;
  std::list<ListenerPtr> listeners_;
};

} // Server
} // Envoy
