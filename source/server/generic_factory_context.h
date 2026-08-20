#pragma once

#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Server {

class GenericFactoryContextImpl : public Server::Configuration::GenericFactoryContext {
public:
  GenericFactoryContextImpl(Server::Configuration::ServerFactoryContext& server_context,
                            ProtobufMessage::ValidationVisitor& validation_visitor);
  GenericFactoryContextImpl(Server::Configuration::GenericFactoryContext& generic_context);

  GenericFactoryContextImpl(Server::Configuration::ServerFactoryContext& server_context,
                            OptRef<Stats::Scope> stats_scope,
                            ProtobufMessage::ValidationVisitor& validation_visitor,
                            OptRef<Init::Manager> init_manager = {})
      : server_context_(server_context),
        stats_scope_(stats_scope.has_value() ? *stats_scope : server_context.scope()),
        validation_visitor_(validation_visitor),
        init_manager_(init_manager.has_value() ? *init_manager : server_context.initManager()) {}
  explicit GenericFactoryContextImpl(Server::Configuration::ServerFactoryContext& server_context);

  // Server::Configuration::GenericFactoryContext
  Server::Configuration::ServerFactoryContext& serverFactoryContext() override;
  Stats::Scope& scope() override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override;
  Init::Manager& initManager() override;

  void setInitManager(Init::Manager& init_manager) { init_manager_.emplace(init_manager); }

private:
  Server::Configuration::ServerFactoryContext& server_context_;
  Stats::Scope& stats_scope_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  OptRef<Init::Manager> init_manager_;
};

using GenericFactoryContextImplPtr = std::unique_ptr<GenericFactoryContextImpl>;

} // namespace Server
} // namespace Envoy
