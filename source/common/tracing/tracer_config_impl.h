#pragma once

#include "envoy/protobuf/message_validator.h"
#include "envoy/server/tracer_config.h"

namespace Envoy {
namespace Tracing {

class TracerFactoryContextImpl : public Server::Configuration::TracerFactoryContext {
public:
  TracerFactoryContextImpl(
      Server::Configuration::ServerFactoryContext& server_factory_context,
      Server::Configuration::TransportSocketFactoryContext& transport_socket_factory_context,
      ProtobufMessage::ValidationVisitor& validation_visitor)
      : server_factory_context_(server_factory_context),
        transport_socket_factory_context_(transport_socket_factory_context),
        validation_visitor_(validation_visitor) {}
  Server::Configuration::ServerFactoryContext& serverFactoryContext() override {
    return server_factory_context_;
  }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return validation_visitor_;
  }
  Server::Configuration::TransportSocketFactoryContext& transportSocketFactoryContext() override {
    return transport_socket_factory_context_;
  }

private:
  Server::Configuration::ServerFactoryContext& server_factory_context_;
  Server::Configuration::TransportSocketFactoryContext& transport_socket_factory_context_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

} // namespace Tracing
} // namespace Envoy
