#pragma once

#include "envoy/server/factory_context.h"
#include "envoy/server/instance.h"

#include "source/server/factory_context_impl.h"

namespace Envoy {
namespace Server {

class AdminFactoryContext final : public FactoryContextImplBase {
public:
  AdminFactoryContext(Envoy::Server::Instance& server,
                      const Network::ListenerInfoConstSharedPtr& listener_info)
      : FactoryContextImplBase(server, server.messageValidationContext().staticValidationVisitor(),
                               server.stats().createScope(""),
                               server.stats().createScope("listener.admin."), listener_info) {}

  Init::Manager& initManager() override {
    // Reuse the server init manager to avoid creating a new one for this special listener.
    return server_.initManager();
  }
  Network::DrainDecision& drainDecision() override {
    // Reuse the server drain manager to avoid creating a new one for this special listener.
    return server_.drainManager();
  }
};
using AdminFactoryContextPtr = std::unique_ptr<AdminFactoryContext>;

} // namespace Server
} // namespace Envoy
