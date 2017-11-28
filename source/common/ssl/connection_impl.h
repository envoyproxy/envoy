#pragma once

#include <cstdint>
#include <string>

#include "envoy/network/transport_socket.h"
#include "common/network/connection_impl.h"
#include "common/ssl/context_impl.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {

enum class InitialState { Client, Server };

class SslSocket : public Network::TransportSocket,
                  public Connection,
                  protected Logger::Loggable<Logger::Id::connection> {
public:
  SslSocket(Context &ctx, InitialState state);

  // Ssl::Connection
  bool peerCertificatePresented() const override;
  std::string uriSanLocalCertificate() override;
  std::string sha256PeerCertificateDigest() override;
  std::string subjectPeerCertificate() const override;
  std::string uriSanPeerCertificate() override;

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks &callbacks) override;
  std::string protocol() const override;
  bool canFlushClose() override { return handshake_complete_; }
  void closeSocket(Network::ConnectionEvent close_type) override;
  Network::IoResult doRead(Buffer::Instance& read_buffer) override;
  Network::IoResult doWrite(Buffer::Instance& write_buffer) override;
  void onConnected() override;

  SSL* rawSslForTest() { return ssl_.get(); }

private:
  Network::PostIoAction doHandshake();
  void drainErrorQueue();
  std::string getUriSanFromCertificate(X509* cert);

  Network::TransportSocketCallbacks* callbacks_{};
  ContextImpl& ctx_;
  bssl::UniquePtr<SSL> ssl_;
  bool handshake_complete_{};
};

class ConnectionImpl : public Network::ConnectionImpl {
 public:
  ConnectionImpl(Event::DispatcherImpl &dispatcher, int fd,
                 Network::Address::InstanceConstSharedPtr remote_address,
                 Network::Address::InstanceConstSharedPtr local_address,
                 Network::Address::InstanceConstSharedPtr bind_to_address, bool using_original_dst,
                 bool connected, Context &ctx, InitialState state);
  ~ConnectionImpl();

  // Network::Connection
  Ssl::Connection *ssl() override { return dynamic_cast<SslSocket*>(transport_socket_.get()); }
  const Ssl::Connection *ssl() const override { return dynamic_cast<SslSocket*>(transport_socket_.get()); }
};


class ClientConnectionImpl final : public ConnectionImpl, public Network::ClientConnection {
public:
  ClientConnectionImpl(Event::DispatcherImpl& dispatcher, Context& ctx,
                       Network::Address::InstanceConstSharedPtr address,
                       Network::Address::InstanceConstSharedPtr source_address);

  // Network::ClientConnection
  void connect() override;
};

} // namespace Ssl
} // namespace Envoy
