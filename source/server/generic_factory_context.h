#pragma once

#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Server {

class GenericFactoryContextImpl : public Server::Configuration::GenericFactoryContext {
public:
  GenericFactoryContextImpl(Server::Configuration::ServerFactoryContext& server_context,
                            ProtobufMessage::ValidationVisitor& validation_visitor);
  GenericFactoryContextImpl(Server::Configuration::FactoryContext& factory_context);
  GenericFactoryContextImpl(const Server::Configuration::GenericFactoryContext& generic_context);

  // Server::Configuration::GenericFactoryContext
  Server::Configuration::ServerFactoryContext& serverFactoryContext() const override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() const override;
  Stats::Scope& scope() const override;
  Init::Manager& initManager() const override;

private:
  Server::Configuration::ServerFactoryContext& server_context_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Stats::Scope& scope_;
  Init::Manager& init_manager_;
};

} // namespace Server
} // namespace Envoy
