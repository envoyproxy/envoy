#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Server {

GenericFactoryContextImpl::GenericFactoryContextImpl(
    Server::Configuration::ServerFactoryContext& server_context,
    ProtobufMessage::ValidationVisitor& validation_visitor)
    : server_context_(server_context), stats_scope_(server_context.serverScope()),
      validation_visitor_(validation_visitor), init_manager_(&server_context.initManager()) {}

GenericFactoryContextImpl::GenericFactoryContextImpl(
    Server::Configuration::GenericFactoryContext& generic_context)
    : server_context_(generic_context.serverFactoryContext()),
      stats_scope_(generic_context.scope()),
      validation_visitor_(generic_context.messageValidationVisitor()),
      init_manager_(&generic_context.initManager()) {}

// Server::Configuration::GenericFactoryContext
Server::Configuration::ServerFactoryContext& GenericFactoryContextImpl::serverFactoryContext() {
  return server_context_;
}
ProtobufMessage::ValidationVisitor& GenericFactoryContextImpl::messageValidationVisitor() {
  return validation_visitor_;
}

Stats::Scope& GenericFactoryContextImpl::scope() { return stats_scope_; }
Init::Manager& GenericFactoryContextImpl::initManager() {
  ASSERT(init_manager_ != nullptr);
  return *init_manager_;
}

} // namespace Server
} // namespace Envoy
