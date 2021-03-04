#pragma once

#include "envoy/extensions/transport_sockets/starttls/v3/starttls.pb.h"
#include "envoy/extensions/transport_sockets/starttls/v3/starttls.pb.validate.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace StartTls {

class StartTlsSocket : public Network::TransportSocket, Logger::Loggable<Logger::Id::filter> {
public:
  StartTlsSocket(const envoy::extensions::transport_sockets::starttls::v3::StartTlsConfig&,
                 Network::TransportSocketPtr raw_socket, // RawBufferSocket
                 Network::TransportSocketPtr tls_socket, // TlsSocket
                 const Network::TransportSocketOptionsSharedPtr&)
      : active_socket_(std::move(raw_socket)), tls_socket_(std::move(tls_socket)) {}

  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override {
    active_socket_->setTransportSocketCallbacks(callbacks);
    callbacks_ = &callbacks;
  }

  std::string protocol() const override { return TransportProtocolNames::get().StartTls; }

  absl::string_view failureReason() const override { return active_socket_->failureReason(); }

  void onConnected() override { active_socket_->onConnected(); }
  bool canFlushClose() override { return active_socket_->canFlushClose(); }
  Ssl::ConnectionInfoConstSharedPtr ssl() const override { return active_socket_->ssl(); }

  void closeSocket(Network::ConnectionEvent event) override {
    return active_socket_->closeSocket(event);
  }

  Network::IoResult doRead(Buffer::Instance& buffer) override {
    return active_socket_->doRead(buffer);
  }

  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override {
    return active_socket_->doWrite(buffer, end_stream);
  }

  // Method to enable TLS.
  bool startSecureTransport() override;

private:
  // Socket used in all transport socket operations.
  // initially it is set to use raw buffer socket but
  // can be converted to use tls.
  Network::TransportSocketPtr active_socket_;
  // Secure transport socket. It will replace raw buffer socket
  //  when startSecureTransport is called.
  Network::TransportSocketPtr tls_socket_;

  Network::TransportSocketCallbacks* callbacks_{};

  bool using_tls_{false};
};

class ServerStartTlsSocketFactory : public Network::TransportSocketFactory,
                                    Logger::Loggable<Logger::Id::config> {
public:
  ~ServerStartTlsSocketFactory() override = default;

  ServerStartTlsSocketFactory(
      const envoy::extensions::transport_sockets::starttls::v3::StartTlsConfig& config,
      Network::TransportSocketFactoryPtr raw_socket_factory,
      Network::TransportSocketFactoryPtr tls_socket_factory)
      : raw_socket_factory_(std::move(raw_socket_factory)),
        tls_socket_factory_(std::move(tls_socket_factory)), config_(config) {}

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr options) const override;
  bool implementsSecureTransport() const override { return false; }
  bool usesProxyProtocolOptions() const override { return false; }

private:
  Network::TransportSocketFactoryPtr raw_socket_factory_;
  Network::TransportSocketFactoryPtr tls_socket_factory_;
  envoy::extensions::transport_sockets::starttls::v3::StartTlsConfig config_;
};

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
