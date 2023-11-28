#include "source/common/config/config_factory_context.h"

namespace Envoy {
namespace Config {

ConfigFactoryContextImpl::ConfigFactoryContextImpl(
    Server::Configuration::ServerFactoryContext& server_context,
    ProtobufMessage::ValidationVisitor& validation_visitor)
    : server_context_(server_context), validation_visitor_(validation_visitor) {}

ConfigFactoryContextImpl::ConfigFactoryContextImpl(
    Server::Configuration::FactoryContext& factory_context)
    : ConfigFactoryContextImpl(factory_context.getServerFactoryContext(),
                               factory_context.messageValidationVisitor()) {}

ConfigFactoryContextImpl::ConfigFactoryContextImpl(
    const Server::Configuration::ConfigFactoryContext& config_context)
    : ConfigFactoryContextImpl(config_context.getServerFactoryContext(),
                               config_context.messageValidationVisitor()) {}

// Server::Configuration::ConfigFactoryContext
Server::Configuration::ServerFactoryContext&
ConfigFactoryContextImpl::getServerFactoryContext() const {
  return server_context_;
}
ProtobufMessage::ValidationVisitor& ConfigFactoryContextImpl::messageValidationVisitor() const {
  return validation_visitor_;
}

} // namespace Config
} // namespace Envoy
