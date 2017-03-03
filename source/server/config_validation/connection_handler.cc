#include "server/config_validation/connection_handler.h"

namespace Server {

ValidationConnectionHandler::ValidationConnectionHandler(Api::ApiPtr&& api)
    : api_(std::move(api)), dispatcher_(api_->allocateDispatcher()) {}

ValidationConnectionHandler::~ValidationConnectionHandler() {
  dispatcher_->clearDeferredDeleteList();
}

void ValidationConnectionHandler::addListener(Network::FilterChainFactory&, Network::ListenSocket&,
                                              Stats::Scope&, const Network::ListenerOptions&) {
  throw EnvoyException("ValidationConnectionHandler::addListener() unimplemented");
}

void ValidationConnectionHandler::addSslListener(Network::FilterChainFactory&, Ssl::ServerContext&,
                                                 Network::ListenSocket&, Stats::Scope&,
                                                 const Network::ListenerOptions&) {
  throw EnvoyException("ValidationConnectionHandler::addSslListener() unimplemented");
}

} // Server
