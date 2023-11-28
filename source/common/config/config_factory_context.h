#pragma once

#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Config {

class ConfigFactoryContextImpl : public Server::Configuration::ConfigFactoryContext {
public:
  ConfigFactoryContextImpl(Server::Configuration::ServerFactoryContext& server_context,
                           ProtobufMessage::ValidationVisitor& validation_visitor);
  ConfigFactoryContextImpl(Server::Configuration::FactoryContext& factory_context);
  ConfigFactoryContextImpl(const Server::Configuration::ConfigFactoryContext& config_context);

  // Server::Configuration::ConfigFactoryContext
  Server::Configuration::ServerFactoryContext& getServerFactoryContext() const override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() const override;

private:
  Server::Configuration::ServerFactoryContext& server_context_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

} // namespace Config
} // namespace Envoy
