#pragma once

#include "envoy/server/factory_context.h"
#include "envoy/server/instance.h"

#include "source/common/config/metadata.h"
#include "source/common/listener_manager/listener_info_impl.h"

namespace Envoy {
namespace Server {

using ListenerInfoConstSharedPtr = std::shared_ptr<const Network::ListenerInfo>;

class FactoryContextImplBase : virtual public Configuration::FactoryContext {
public:
  FactoryContextImplBase(Server::Instance& server,
                         ProtobufMessage::ValidationVisitor& validation_visitor,
                         Stats::ScopeSharedPtr scope, Stats::ScopeSharedPtr listener_scope,
                         ListenerInfoConstSharedPtr listener_info = nullptr);

  // Configuration::FactoryContext
  Configuration::ServerFactoryContext& serverFactoryContext() const override;
  Stats::Scope& scope() override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() const override;
  Configuration::TransportSocketFactoryContext& getTransportSocketFactoryContext() const override;
  const Network::ListenerInfo& listenerInfo() const override;

  Stats::Scope& listenerScope() override;

protected:
  Server::Instance& server_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  // Listener scope without the listener prefix.
  Stats::ScopeSharedPtr scope_;
  // Listener scope with the listener prefix.
  Stats::ScopeSharedPtr listener_scope_;
  ListenerInfoConstSharedPtr listener_info_;
};

/**
 * Implementation of FactoryContext wrapping a Server::Instance and some listener components.
 */
class FactoryContextImpl : public FactoryContextImplBase {
public:
  FactoryContextImpl(Server::Instance& server, Network::DrainDecision& drain_decision,
                     Stats::ScopeSharedPtr scope, Stats::ScopeSharedPtr listener_scope,
                     ListenerInfoConstSharedPtr listener_info);

  // Configuration::FactoryContext
  Init::Manager& initManager() override;
  Network::DrainDecision& drainDecision() override;

private:
  Network::DrainDecision& drain_decision_;
};

} // namespace Server
} // namespace Envoy
