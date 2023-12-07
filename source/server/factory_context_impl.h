#pragma once

#include "envoy/server/factory_context.h"
#include "envoy/server/instance.h"

#include "source/common/config/metadata.h"

namespace Envoy {
namespace Server {

/**
 * Implementation of FactoryContext wrapping a Server::Instance and some listener components.
 */
class FactoryContextImpl : public Configuration::FactoryContext {
public:
  FactoryContextImpl(Server::Instance& server, const envoy::config::listener::v3::Listener& config,
                     Network::DrainDecision& drain_decision, Stats::Scope& global_scope,
                     Stats::Scope& listener_scope, bool is_quic);

  // Configuration::FactoryContext
  AccessLog::AccessLogManager& accessLogManager() override;
  Upstream::ClusterManager& clusterManager() override;
  Event::Dispatcher& mainThreadDispatcher() override;
  const Server::Options& options() override;
  Grpc::Context& grpcContext() override;
  Router::Context& routerContext() override;
  bool healthCheckFailed() override;
  Http::Context& httpContext() override;
  Init::Manager& initManager() override;
  const LocalInfo::LocalInfo& localInfo() const override;
  Envoy::Runtime::Loader& runtime() override;
  Stats::Scope& scope() override;
  Stats::Scope& serverScope() override { return *server_.stats().rootScope(); }
  Singleton::Manager& singletonManager() override;
  OverloadManager& overloadManager() override;
  ThreadLocal::SlotAllocator& threadLocal() override;
  OptRef<Admin> admin() override;
  TimeSource& timeSource() override;
  ProtobufMessage::ValidationContext& messageValidationContext() override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override;
  Api::Api& api() override;
  ServerLifecycleNotifier& lifecycleNotifier() override;
  ProcessContextOptRef processContext() override;
  Configuration::ServerFactoryContext& serverFactoryContext() const override;
  Configuration::TransportSocketFactoryContext& getTransportSocketFactoryContext() const override;
  const Network::ListenerInfo& listenerInfo() const override;
  Network::DrainDecision& drainDecision() override;
  Stats::Scope& listenerScope() override;

private:
  class ListenerInfoImpl : public Network::ListenerInfo {
  public:
    explicit ListenerInfoImpl(bool is_quic) : is_quic_(is_quic) {}
    const envoy::config::core::v3::Metadata& metadata() const override {
      return metadata_.proto_metadata_;
    }
    const Envoy::Config::TypedMetadata& typedMetadata() const override {
      return metadata_.typed_metadata_;
    }
    envoy::config::core::v3::TrafficDirection direction() const override {
      return envoy::config::core::v3::UNSPECIFIED;
    }
    bool isQuic() const override { return is_quic_; }

  private:
    const bool is_quic_;
    Envoy::Config::MetadataPack<Envoy::Network::ListenerTypedMetadataFactory> metadata_;
  };
  Server::Instance& server_;
  Network::DrainDecision& drain_decision_;
  Stats::Scope& global_scope_;
  Stats::Scope& listener_scope_;
  ListenerInfoImpl listener_info_;
};

} // namespace Server
} // namespace Envoy
