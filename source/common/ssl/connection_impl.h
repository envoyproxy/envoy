#pragma once

#include "context_impl.h"

#include "common/network/connection_impl.h"

namespace Ssl {

class ConnectionImpl : public Network::ConnectionImpl, public Connection {
public:
  ConnectionImpl(Event::DispatcherImpl& dispatcher, Event::Libevent::BufferEventPtr&& bev,
                 const std::string& remote_address, ContextImpl& ctx);
  ~ConnectionImpl();

  // Network::Connection
  std::string nextProtocol() override;
  void onEvent(short events) override;
  Ssl::Connection* ssl() override { return this; }

  // Network::ConnectionImpl
  void closeBev() override;

  // Ssl::Connection
  std::string sha256PeerCertificateDigest() override;

private:
  ContextImpl& ctx_;
  bool handshake_complete_{};
};

class ClientConnectionImpl final : public ConnectionImpl, public Network::ClientConnection {
public:
  ClientConnectionImpl(Event::DispatcherImpl& dispatcher, Event::Libevent::BufferEventPtr&& bev,
                       ContextImpl& ctx, const std::string& url);

  // Network::ClientConnection
  void connect() override;
};

} // Ssl
