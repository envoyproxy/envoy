#include "source/server/factory_context_impl.h"

namespace Envoy {
namespace Server {

FactoryContextImplBase::FactoryContextImplBase(
    Server::Instance& server, ProtobufMessage::ValidationVisitor& validation_visitor,
    Stats::ScopeSharedPtr scope, Stats::ScopeSharedPtr listener_scope,
    const Network::ListenerInfoConstSharedPtr& listener_info)
    : server_(server), validation_visitor_(validation_visitor), scope_(std::move(scope)),
      listener_scope_(std::move(listener_scope)), listener_info_(listener_info) {
  ASSERT(listener_info_ != nullptr);
}

Configuration::ServerFactoryContext& FactoryContextImplBase::serverFactoryContext() const {
  return server_.serverFactoryContext();
}

ProtobufMessage::ValidationVisitor& FactoryContextImplBase::messageValidationVisitor() const {
  return validation_visitor_;
}

Stats::Scope& FactoryContextImplBase::scope() { return *scope_; }

Configuration::TransportSocketFactoryContext&
FactoryContextImplBase::getTransportSocketFactoryContext() const {
  return server_.transportSocketFactoryContext();
}

Stats::Scope& FactoryContextImplBase::listenerScope() { return *listener_scope_; }

const Network::ListenerInfo& FactoryContextImplBase::listenerInfo() const {
  return *listener_info_;
}

FactoryContextImpl::FactoryContextImpl(Server::Instance& server,
                                       Network::DrainDecision& drain_decision,
                                       Stats::ScopeSharedPtr scope,
                                       Stats::ScopeSharedPtr listener_scope,
                                       const Network::ListenerInfoConstSharedPtr& listener_info)
    : FactoryContextImplBase(server, server.messageValidationContext().staticValidationVisitor(),
                             std::move(scope), std::move(listener_scope), listener_info),
      drain_decision_(drain_decision) {}

Init::Manager& FactoryContextImpl::initManager() { return server_.initManager(); }

Network::DrainDecision& FactoryContextImpl::drainDecision() { return drain_decision_; }

} // namespace Server
} // namespace Envoy
