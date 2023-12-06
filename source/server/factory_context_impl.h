#pragma once

#include "envoy/server/factory_context.h"
#include "envoy/server/instance.h"

#include "source/common/config/metadata.h"

namespace Envoy {
namespace Server {

using ListenerMetadataPack =
    Envoy::Config::MetadataPack<Envoy::Network::ListenerTypedMetadataFactory>;

class FactoryContextImplBase : virtual public Configuration::FactoryContext {
public:
  FactoryContextImplBase(Server::Instance& server,
                         ProtobufMessage::ValidationVisitor& validation_visitor,
                         Stats::ScopeSharedPtr scope, Stats::ScopeSharedPtr listener_scope,
                         const envoy::config::core::v3::Metadata& metadata,
                         envoy::config::core::v3::TrafficDirection direction, bool is_quic);

  // Configuration::FactoryContext
  Configuration::ServerFactoryContext& serverFactoryContext() const override;
  Stats::Scope& scope() override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() const override;
  Configuration::TransportSocketFactoryContext& getTransportSocketFactoryContext() const override;
  const envoy::config::core::v3::Metadata& listenerMetadata() const override;
  const Envoy::Config::TypedMetadata& listenerTypedMetadata() const override;
  envoy::config::core::v3::TrafficDirection direction() const override;
  Stats::Scope& listenerScope() override;
  bool isQuicListener() const override;

protected:
  Server::Instance& server_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  // Listener scope without the listener prefix.
  Stats::ScopeSharedPtr scope_;
  // Listener scope with the listener prefix.
  Stats::ScopeSharedPtr listener_scope_;

  const ListenerMetadataPack metadata_;
  const bool is_quic_;
  const envoy::config::core::v3::TrafficDirection direction_;
};

/**
 * Implementation of FactoryContext wrapping a Server::Instance and some listener components.
 */
class FactoryContextImpl : public FactoryContextImplBase {
public:
  FactoryContextImpl(Server::Instance& server, const envoy::config::listener::v3::Listener& config,
                     Network::DrainDecision& drain_decision, Stats::ScopeSharedPtr global_scope,
                     Stats::ScopeSharedPtr listener_scope, bool is_quic);

  // Configuration::FactoryContext
  Init::Manager& initManager() override;
  Network::DrainDecision& drainDecision() override;

private:
  Network::DrainDecision& drain_decision_;
};

} // namespace Server
} // namespace Envoy
