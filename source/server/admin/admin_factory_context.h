#pragma once

#include "envoy/server/factory_context.h"
#include "envoy/server/instance.h"

#include "source/common/listener_manager/listener_info_impl.h"

namespace Envoy {
namespace Server {

class AdminFactoryContext final : public Configuration::FactoryContext {
public:
  AdminFactoryContext(Envoy::Server::Instance& server)
      : server_(server), scope_(server_.stats().createScope("")),
        listener_scope_(server_.stats().createScope("listener.admin.")) {}

  AccessLog::AccessLogManager& accessLogManager() override { return server_.accessLogManager(); }
  Upstream::ClusterManager& clusterManager() override { return server_.clusterManager(); }
  Event::Dispatcher& mainThreadDispatcher() override { return server_.dispatcher(); }
  const Server::Options& options() override { return server_.options(); }
  Grpc::Context& grpcContext() override { return server_.grpcContext(); }
  bool healthCheckFailed() override { return server_.healthCheckFailed(); }
  Http::Context& httpContext() override { return server_.httpContext(); }
  Router::Context& routerContext() override { return server_.routerContext(); }
  const LocalInfo::LocalInfo& localInfo() const override { return server_.localInfo(); }
  Envoy::Runtime::Loader& runtime() override { return server_.runtime(); }
  Stats::Scope& serverScope() override { return *server_.stats().rootScope(); }
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    return server_.messageValidationContext();
  }
  Singleton::Manager& singletonManager() override { return server_.singletonManager(); }
  OverloadManager& overloadManager() override { return server_.overloadManager(); }
  ThreadLocal::Instance& threadLocal() override { return server_.threadLocal(); }
  OptRef<Admin> admin() override { return OptRef<Admin>(server_.admin()); }
  TimeSource& timeSource() override { return server_.timeSource(); }
  Api::Api& api() override { return server_.api(); }
  ServerLifecycleNotifier& lifecycleNotifier() override { return server_.lifecycleNotifier(); }
  ProcessContextOptRef processContext() override { return server_.processContext(); }

  Configuration::ServerFactoryContext& serverFactoryContext() const override {
    return server_.serverFactoryContext();
  }
  Configuration::TransportSocketFactoryContext& getTransportSocketFactoryContext() const override {
    return server_.transportSocketFactoryContext();
  }
  Stats::Scope& scope() override { return *scope_; }
  Stats::Scope& listenerScope() override { return *listener_scope_; }
  const Network::ListenerInfo& listenerInfo() const override { return listener_info_; }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    // Always use the static validation visitor for the admin handler.
    return server_.messageValidationContext().staticValidationVisitor();
  }
  Init::Manager& initManager() override {
    // Reuse the server init manager to avoid creating a new one for this special listener.
    return server_.initManager();
  }
  Network::DrainDecision& drainDecision() override {
    // Reuse the server drain manager to avoid creating a new one for this special listener.
    return server_.drainManager();
  }

private:
  Envoy::Server::Instance& server_;

  // Listener scope without the listener prefix.
  Stats::ScopeSharedPtr scope_;
  // Listener scope with the listener prefix.
  Stats::ScopeSharedPtr listener_scope_;

  const ListenerInfoImpl listener_info_;
};
using AdminFactoryContextPtr = std::unique_ptr<AdminFactoryContext>;

} // namespace Server
} // namespace Envoy
