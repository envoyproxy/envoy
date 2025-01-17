#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Server {

GenericFactoryContextImpl::GenericFactoryContextImpl(
    Server::Configuration::ServerFactoryContext& server_context,
    ProtobufMessage::ValidationVisitor& validation_visitor)
    : server_context_(server_context), validation_visitor_(validation_visitor),
      scope_(server_context.serverScope()), init_manager_(server_context.initManager()) {}

GenericFactoryContextImpl::GenericFactoryContextImpl(
    Server::Configuration::GenericFactoryContext& generic_context)
    : server_context_(generic_context.serverFactoryContext()),
      validation_visitor_(generic_context.messageValidationVisitor()),
      scope_(generic_context.scope()), init_manager_(generic_context.initManager()) {}

// Server::Configuration::GenericFactoryContext
Server::Configuration::ServerFactoryContext&
GenericFactoryContextImpl::serverFactoryContext() const {
  return server_context_;
}
ProtobufMessage::ValidationVisitor& GenericFactoryContextImpl::messageValidationVisitor() const {
  return validation_visitor_;
}

Stats::Scope& GenericFactoryContextImpl::scope() { return scope_; }
Init::Manager& GenericFactoryContextImpl::initManager() { return init_manager_; }

} // namespace Server
} // namespace Envoy
