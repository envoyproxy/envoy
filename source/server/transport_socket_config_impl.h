#pragma once

#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Implementation of TransportSocketFactoryContext.
 */
class TransportSocketFactoryContextImpl : public TransportSocketFactoryContext {
public:
  TransportSocketFactoryContextImpl(Server::Configuration::ServerFactoryContext& server_context,
                                    Stats::Scope& stats_scope,
                                    ProtobufMessage::ValidationVisitor& validation_visitor)
      : server_context_(server_context), stats_scope_(stats_scope),
        validation_visitor_(validation_visitor) {}

  /**
   * Pass an init manager to register dynamic secret provider.
   * @param init_manager instance of init manager.
   */
  void setInitManager(Init::Manager& init_manager) { init_manager_ = &init_manager; }

  // TransportSocketFactoryContext
  ServerFactoryContext& serverFactoryContext() override { return server_context_; }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return validation_visitor_;
  }
  Stats::Scope& statsScope() override { return stats_scope_; }
  Init::Manager& initManager() override {
    ASSERT(init_manager_ != nullptr);
    return *init_manager_;
  }

private:
  Server::Configuration::ServerFactoryContext& server_context_;
  Stats::Scope& stats_scope_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;

  Init::Manager* init_manager_{};
};

using TransportSocketFactoryContextImplPtr = std::unique_ptr<TransportSocketFactoryContextImpl>;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
