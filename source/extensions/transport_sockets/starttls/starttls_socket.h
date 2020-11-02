#include "envoy/network/transport_socket.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "extensions/transport_sockets/well_known_names.h"
#include "envoy/extensions/transport_sockets/starttls/v3/starttls.pb.h"
#include "envoy/extensions/transport_sockets/starttls/v3/starttls.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace StartTls {

class StartTlsSocket : public Network::TransportSocket, Logger::Loggable<Logger::Id::filter> {
public:
  StartTlsSocket(const envoy::extensions::transport_sockets::starttls::v3::StartTlsConfig& config,
		 Network::TransportSocketPtr raw_socket, // RawBufferSocket
                 Network::TransportSocketPtr ssl_socket, // SslSocket
                 const Network::TransportSocketOptionsSharedPtr& /*transport_socket_options*/)
      : raw_socket_(std::move(raw_socket)), ssl_socket_(std::move(ssl_socket)) {
      max_cleartext_bytes_ = config.max_cleartext_bytes();}

  virtual void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
    raw_socket_->setTransportSocketCallbacks(callbacks);
    callbacks_ = &callbacks;
    //startSecureTransport();
  }

  // TODO: delegate it to passthrough_
  virtual std::string protocol() const { return TransportProtocolNames::get().StartTls; }

  virtual absl::string_view failureReason() const { return absl::string_view(); }

  virtual bool canFlushClose() {
    if (passthrough_)
      return passthrough_->canFlushClose();
    return raw_socket_->canFlushClose();
  }

  virtual void closeSocket(Network::ConnectionEvent event) {
    if (passthrough_)
      return passthrough_->closeSocket(event);
    return raw_socket_->closeSocket(event);
  }

  // this is called by the outer select loop when the underlying socket is readable?
  virtual Network::IoResult doRead(Buffer::Instance& buffer);
  virtual Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream);

  virtual void onConnected() { raw_socket_->onConnected(); }
  virtual bool startSecureTransport();

  virtual Ssl::ConnectionInfoConstSharedPtr ssl() const {
    if (passthrough_)
      return passthrough_->ssl();
    return raw_socket_->ssl();
  }

private:
  Network::TransportSocketCallbacks* callbacks_{};
  Network::TransportSocketPtr raw_socket_;
  Network::TransportSocketPtr ssl_socket_;
  Network::TransportSocketPtr passthrough_;

  std::string command_buffer_;
  std::string response_buffer_;

  bool switch_to_ssl_{false};
  uint32_t max_cleartext_bytes_{};
};

class ServerStartTlsSocketFactory : public Network::TransportSocketFactory,
                                    Logger::Loggable<Logger::Id::config> {
public:
  virtual ~ServerStartTlsSocketFactory();

  ServerStartTlsSocketFactory(
  const envoy::extensions::transport_sockets::starttls::v3::StartTlsConfig& config,
      //      Stats::Scope& stats_scope,
      const std::vector<std::string>& server_names,
      Network::TransportSocketFactoryPtr raw_socket_factory,
      Network::TransportSocketFactoryPtr tls_socket_factory)
      : // stats_scope_(stats_scope),
        server_names_(server_names), raw_socket_factory_(std::move(raw_socket_factory)),
        tls_socket_factory_(std::move(tls_socket_factory)), config_(config) {}

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr options) const override;
  bool implementsSecureTransport() const override { return false; } // XXX

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
