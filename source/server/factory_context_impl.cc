#include "source/server/factory_context_impl.h"

namespace Envoy {
namespace Server {

FactoryContextImplBase::FactoryContextImplBase(Server::Instance& server,
                                               Stats::ScopeSharedPtr scope,
                                               Stats::ScopeSharedPtr listener_scope,
                                               const envoy::config::core::v3::Metadata& metadata,
                                               envoy::config::core::v3::TrafficDirection direction,
                                               bool is_quic)
    : server_(server), scope_(std::move(scope)), listener_scope_(std::move(listener_scope)),
      metadata_(metadata), is_quic_(is_quic), direction_(direction) {}

Configuration::ServerFactoryContext& FactoryContextImplBase::serverFactoryContext() const {
  return server_.serverFactoryContext();
}

Stats::Scope& FactoryContextImplBase::scope() { return *scope_; }

Configuration::TransportSocketFactoryContext&
FactoryContextImplBase::getTransportSocketFactoryContext() const {
  return server_.transportSocketFactoryContext();
}

const envoy::config::core::v3::Metadata& FactoryContextImplBase::listenerMetadata() const {
  return metadata_.proto_metadata_;
}
const Envoy::Config::TypedMetadata& FactoryContextImplBase::listenerTypedMetadata() const {
  return metadata_.typed_metadata_;
}

envoy::config::core::v3::TrafficDirection FactoryContextImplBase::direction() const {
  return direction_;
}

Stats::Scope& FactoryContextImplBase::listenerScope() { return *listener_scope_; }

bool FactoryContextImplBase::isQuicListener() const { return is_quic_; }

FactoryContextImpl::FactoryContextImpl(Server::Instance& server,
                                       const envoy::config::listener::v3::Listener& config,
                                       Network::DrainDecision& drain_decision,
                                       Stats::ScopeSharedPtr scope,
                                       Stats::ScopeSharedPtr listener_scope, bool is_quic)
    : FactoryContextImplBase(server, std::move(scope), std::move(listener_scope), config.metadata(),
                             config.traffic_direction(), is_quic),
      drain_decision_(drain_decision) {}

Init::Manager& FactoryContextImpl::initManager() { return server_.initManager(); }

ProtobufMessage::ValidationVisitor& FactoryContextImpl::messageValidationVisitor() const {
  return server_.messageValidationContext().staticValidationVisitor();
}

Network::DrainDecision& FactoryContextImpl::drainDecision() { return drain_decision_; }

} // namespace Server
} // namespace Envoy
