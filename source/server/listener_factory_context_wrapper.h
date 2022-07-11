#pragma once

#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Server {

// Wraps the global server context with a listener-local version, so that
// downstream filters can access the downstream factory context.
class ListenerFactoryContextWrapper : public Configuration::ServerFactoryContext {
public:
  ListenerFactoryContextWrapper(Configuration::ServerFactoryContext& context,
                                Configuration::DownstreamFactoryContext& downstream_context)
      : context_(context), downstream_context_(downstream_context) {}

  // Configuration::ServerFactoryContext
  const Options& options() override { return context_.options(); }
  Event::Dispatcher& mainThreadDispatcher() override { return context_.mainThreadDispatcher(); }
  Api::Api& api() override { return context_.api(); }
  const LocalInfo::LocalInfo& localInfo() const override { return context_.localInfo(); }
  Server::Admin& admin() override { return context_.admin(); }
  Envoy::Runtime::Loader& runtime() override { return context_.runtime(); }
  Singleton::Manager& singletonManager() override { return context_.singletonManager(); }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return context_.messageValidationVisitor();
  }
  Stats::Scope& scope() override { return context_.scope(); }
  Stats::Scope& serverScope() override { return context_.serverScope(); }
  ThreadLocal::SlotAllocator& threadLocal() override { return context_.threadLocal(); }
  Upstream::ClusterManager& clusterManager() override { return context_.clusterManager(); }
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    return context_.messageValidationContext();
  }
  TimeSource& timeSource() override { return context_.timeSource(); }
  AccessLog::AccessLogManager& accessLogManager() override { return context_.accessLogManager(); }
  ServerLifecycleNotifier& lifecycleNotifier() override { return context_.lifecycleNotifier(); }
  Init::Manager& initManager() override { return context_.initManager(); }
  Grpc::Context& grpcContext() override { return context_.grpcContext(); }
  Router::Context& routerContext() override { return context_.routerContext(); }
  Envoy::Server::DrainManager& drainManager() override { return context_.drainManager(); }
  Configuration::StatsConfig& statsConfig() override { return context_.statsConfig(); }
  envoy::config::bootstrap::v3::Bootstrap& bootstrap() override { return context_.bootstrap(); }
  Http::Context& httpContext() override { return context_.httpContext(); }
  bool healthCheckFailed() override { return context_.healthCheckFailed(); }
  ProcessContextOptRef processContext() override { return context_.processContext(); }
  OptRef<Configuration::DownstreamFactoryContext> downstreamContext() override {
    return {downstream_context_};
  }

private:
  Configuration::ServerFactoryContext& context_;
  Configuration::DownstreamFactoryContext& downstream_context_;
};

} // namespace Server
} // namespace Envoy
