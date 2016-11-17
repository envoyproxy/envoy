#pragma once

#include "context_impl.h"

#include "common/network/connection_impl.h"

namespace Ssl {

class ConnectionImpl : public Network::ConnectionImpl, public Connection {
public:
  enum class InitialState { Client, Server };

  ConnectionImpl(Event::DispatcherImpl& dispatcher, int fd, const std::string& remote_address,
                 Context& ctx, InitialState state);
  ~ConnectionImpl();

  // Network::Connection
  std::string nextProtocol() override;
  Ssl::Connection* ssl() override { return this; }

  // Ssl::Connection
  std::string sha256PeerCertificateDigest() override;

private:
  PostIoAction doHandshake();
  void drainErrorQueue();

  // Network::ConnectionImpl
  void closeSocket(uint32_t close_type) override;
  IoResult doReadFromSocket() override;
  IoResult doWriteToSocket() override;
  void onConnected() override;

  ContextImpl& ctx_;
  SslConPtr ssl_;
  bool handshake_complete_{};
};

class ClientConnectionImpl final : public ConnectionImpl, public Network::ClientConnection {
public:
  ClientConnectionImpl(Event::DispatcherImpl& dispatcher, Context& ctx, const std::string& url);

  // Network::ClientConnection
  void connect() override;
};

} // Ssl
