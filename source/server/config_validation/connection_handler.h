#pragma once

#include "envoy/api/api.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"

namespace Envoy {
namespace Server {

/**
 * Config-validation implementation of ConnectionHandler. Calls to add*Listener() will fail, since
 * no listeners can be added at validation time.
 */
class ValidationConnectionHandler : public Network::ConnectionHandler {
public:
  ValidationConnectionHandler(Api::ApiPtr&& api);
  ~ValidationConnectionHandler();

  Api::Api& api() { return *api_; }
  Event::Dispatcher& dispatcher() { return *dispatcher_; }

  // Network::ConnectionHandler
  uint64_t numConnections() override { return 0; }
  void addListener(Network::FilterChainFactory&, Network::ListenSocket&, Stats::Scope&,
                   const Network::ListenerOptions&) override;
  void addSslListener(Network::FilterChainFactory&, Ssl::ServerContext&, Network::ListenSocket&,
                      Stats::Scope&, const Network::ListenerOptions&) override;
  Network::Listener* findListenerByAddress(const Network::Address::Instance&) override {
    return nullptr;
  }
  void closeListeners() override{};

private:
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
};

} // Server
} // Envoy
