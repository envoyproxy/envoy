#pragma once

#include "envoy/extensions/transport_sockets/starttls/v3/starttls.pb.h"
#include "envoy/extensions/transport_sockets/starttls/v3/starttls.pb.validate.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/network/transport_socket_options_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace StartTls {

class StartTlsSocket : public Network::TransportSocket, Logger::Loggable<Logger::Id::filter> {
public:
  StartTlsSocket(Network::TransportSocketPtr raw_socket, // RawBufferSocket
                 Network::TransportSocketPtr tls_socket, // TlsSocket
                 const Network::TransportSocketOptionsConstSharedPtr&)
      : active_socket_(std::move(raw_socket)), tls_socket_(std::move(tls_socket)) {}

  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override {
    active_socket_->setTransportSocketCallbacks(callbacks);
    callbacks_ = &callbacks;
  }

  std::string protocol() const override { return "starttls"; }

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

  void configureInitialCongestionWindow(uint64_t bandwidth_bits_per_sec,
                                        std::chrono::microseconds rtt) override {
    return active_socket_->configureInitialCongestionWindow(bandwidth_bits_per_sec, rtt);
  }

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

class StartTlsSocketFactory : public Network::CommonTransportSocketFactory,
                              Logger::Loggable<Logger::Id::config> {
public:
  ~StartTlsSocketFactory() override = default;

  StartTlsSocketFactory(Network::TransportSocketFactoryPtr raw_socket_factory,
                        Network::TransportSocketFactoryPtr tls_socket_factory)
      : raw_socket_factory_(std::move(raw_socket_factory)),
        tls_socket_factory_(std::move(tls_socket_factory)) {}

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options) const override;
  bool implementsSecureTransport() const override { return false; }

private:
  Network::TransportSocketFactoryPtr raw_socket_factory_;
  Network::TransportSocketFactoryPtr tls_socket_factory_;
};

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
