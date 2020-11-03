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
  StartTlsSocket(const envoy::extensions::transport_sockets::starttls::v3::StartTlsConfig& config,
                 Network::TransportSocketPtr raw_socket, // RawBufferSocket
                 Network::TransportSocketPtr ssl_socket, // SslSocket
                 const Network::TransportSocketOptionsSharedPtr&)
      : passthrough_(std::move(raw_socket)), ssl_socket_(std::move(ssl_socket)) {
    max_cleartext_bytes_ = config.max_cleartext_bytes();
  }

  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override {
    passthrough_->setTransportSocketCallbacks(callbacks);
    callbacks_ = &callbacks;
  }

  std::string protocol() const override { return TransportProtocolNames::get().StartTls; }

  absl::string_view failureReason() const override { return passthrough_->failureReason(); }

  void onConnected() override { passthrough_->onConnected(); }
  bool canFlushClose() override { return passthrough_->canFlushClose(); }
  Ssl::ConnectionInfoConstSharedPtr ssl() const override { return passthrough_->ssl(); }

  void closeSocket(Network::ConnectionEvent event) override {
    return passthrough_->closeSocket(event);
  }

  Network::IoResult doRead(Buffer::Instance& buffer) override {
    return passthrough_->doRead(buffer);
  }

  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override {
    return passthrough_->doWrite(buffer, end_stream);
  }

  // Method to enable TLS.
  bool startSecureTransport() override;

private:
  // Socket used in all transport socket operations.
  // initially it is set to use raw buffer socket but
  // can be converted to use ssl.
  Network::TransportSocketPtr passthrough_;
  // Secure transport socket. It will replace raw buffer socket
  // initially stored in passthrough_, when startSecureTransport is called.
  Network::TransportSocketPtr ssl_socket_;

  Network::TransportSocketCallbacks* callbacks_{};

  bool using_ssl_{false};

  // Max number of clear-text bytes allowed to be exchanged between a client and StartTlsSocket.
  // It serves as fuse. If there is misconfiguration, it will stop passing clear-text data.
  uint32_t max_cleartext_bytes_{};
};

class ServerStartTlsSocketFactory : public Network::TransportSocketFactory,
                                    Logger::Loggable<Logger::Id::config> {
public:
  virtual ~ServerStartTlsSocketFactory() {}

  ServerStartTlsSocketFactory(
      const envoy::extensions::transport_sockets::starttls::v3::StartTlsConfig& config,
      const std::vector<std::string>& server_names,
      Network::TransportSocketFactoryPtr raw_socket_factory,
      Network::TransportSocketFactoryPtr tls_socket_factory)
      : server_names_(server_names), raw_socket_factory_(std::move(raw_socket_factory)),
        tls_socket_factory_(std::move(tls_socket_factory)), config_(config) {}

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr options) const override;
  bool implementsSecureTransport() const override { return false; }

private:
  const std::vector<std::string> server_names_;
  Network::TransportSocketFactoryPtr raw_socket_factory_;
  Network::TransportSocketFactoryPtr tls_socket_factory_;
  envoy::extensions::transport_sockets::starttls::v3::StartTlsConfig config_;
};

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
