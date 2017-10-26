#pragma once

#include <cstdint>
#include <string>

#include "common/network/connection_impl.h"
#include "common/ssl/context_impl.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {

class ConnectionImpl : public Network::ConnectionImpl {
public:
  enum class InitialState { Client, Server };

  ConnectionImpl(Event::DispatcherImpl& dispatcher, int fd,
                 Network::Address::InstanceConstSharedPtr remote_address,
                 Network::Address::InstanceConstSharedPtr local_address,
                 Network::Address::InstanceConstSharedPtr bind_to_address, bool using_original_dst,
                 bool connected, Context& ctx, InitialState state);
};

class ClientConnectionImpl final : public ConnectionImpl, public Network::ClientConnection {
public:
  ClientConnectionImpl(Event::DispatcherImpl& dispatcher, Context& ctx,
                       Network::Address::InstanceConstSharedPtr address,
                       Network::Address::InstanceConstSharedPtr source_address);

  // Network::ClientConnection
  void connect() override;
};

class Tls : public Network::TransportSecurity,
            public Connection,
            protected Logger::Loggable<Logger::Id::connection> {
public:
  enum class InitialState { Client, Server };

  Tls(Network::TransportSecurityCallbacks& callbacks, Context& ctx, InitialState state);

  // Network::TransportSecurity
  std::string nextProtocol() const override;
  Network::Connection::IoResult doReadFromSocket() override;
  Network::Connection::IoResult doWriteToSocket() override;
  void onConnected() override;
  void closeSocket(Network::ConnectionEvent close_type) override;

  // Ssl::Connection
  bool peerCertificatePresented() override;
  std::string uriSanLocalCertificate() override;
  std::string sha256PeerCertificateDigest() override;
  std::string subjectPeerCertificate() override;
  std::string uriSanPeerCertificate() override;

  SSL* rawSslForTest() { return ssl_.get(); }

private:
  Network::Connection::PostIoAction doHandshake();
  void drainErrorQueue();
  std::string getUriSanFromCertificate(X509* cert);

  Network::TransportSecurityCallbacks& callbacks_;
  ContextImpl& ctx_;
  bssl::UniquePtr<SSL> ssl_;
  bool handshake_complete_{};
};

} // namespace Ssl
} // namespace Envoy
