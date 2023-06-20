#pragma once

#include "envoy/extensions/transport_sockets/starttls/v3/starttls.pb.h"
#include "envoy/extensions/transport_sockets/starttls/v3/starttls.pb.validate.h"
#include "envoy/network/connection.h"
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
    callbacks_ = &callbacks;
    active_socket_->setTransportSocketCallbacks(callbacks_);
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
  // This is a proxy for wrapping the transport callback object passed from the consumer.
  // Its primary purpose is to filter Connected events to ensure they only happen once per open.
  // connection open.
  class CallbackProxy : public Network::TransportSocketCallbacks {
  public:
    CallbackProxy(Network::TransportSocketCallbacks* callbacks) : parent_(callbacks) {}

    Network::IoHandle& ioHandle() override { return parent_->ioHandle(); }
    const Network::IoHandle& ioHandle() const override {
      return static_cast<const Network::TransportSocketCallbacks*>(parent_)->ioHandle();
    }
    Network::Connection& connection() override { return parent_->connection(); }
    bool shouldDrainReadBuffer() override { return parent_->shouldDrainReadBuffer(); }
    void setTransportSocketIsReadable() override { return parent_->setTransportSocketIsReadable(); }
    void raiseEvent(Network::ConnectionEvent event) override {
      if (event == Network::ConnectionEvent::Connected) {
        // Don't send the connected event if we're already open
        if (connected_) {
          parent_->flushWriteBuffer();
          return;
        }
        connected_ = true;
      } else {
        connected_ = false;
      }

      parent_->raiseEvent(event);
    }
    void flushWriteBuffer() override { parent_->flushWriteBuffer(); }

  private:
    Network::TransportSocketCallbacks* parent_;
    bool connected_{false};
  };

  // Socket used in all transport socket operations.
  // initially it is set to use raw buffer socket but
  // can be converted to use tls.
  Network::TransportSocketPtr active_socket_;
  // Secure transport socket. It will replace raw buffer socket
  //  when startSecureTransport is called.
  Network::TransportSocketPtr tls_socket_;

  CallbackProxy callbacks_{nullptr};

  bool using_tls_{false};
};

class StartTlsSocketFactory : public Network::CommonUpstreamTransportSocketFactory,
                              Logger::Loggable<Logger::Id::config> {
public:
  ~StartTlsSocketFactory() override = default;

  StartTlsSocketFactory(Network::UpstreamTransportSocketFactoryPtr raw_socket_factory,
                        Network::UpstreamTransportSocketFactoryPtr tls_socket_factory)
      : raw_socket_factory_(std::move(raw_socket_factory)),
        tls_socket_factory_(std::move(tls_socket_factory)) {}

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        Upstream::HostDescriptionConstSharedPtr host) const override;
  bool implementsSecureTransport() const override { return false; }
  absl::string_view defaultServerNameIndication() const override { return ""; }
  Envoy::Ssl::ClientContextSharedPtr sslCtx() override { return tls_socket_factory_->sslCtx(); }
  OptRef<const Ssl::ClientContextConfig> clientContextConfig() const override {
    return tls_socket_factory_->clientContextConfig();
  }

private:
  Network::UpstreamTransportSocketFactoryPtr raw_socket_factory_;
  Network::UpstreamTransportSocketFactoryPtr tls_socket_factory_;
};

class StartTlsDownstreamSocketFactory : public Network::DownstreamTransportSocketFactory,
                                        Logger::Loggable<Logger::Id::config> {
public:
  ~StartTlsDownstreamSocketFactory() override = default;

  StartTlsDownstreamSocketFactory(Network::DownstreamTransportSocketFactoryPtr raw_socket_factory,
                                  Network::DownstreamTransportSocketFactoryPtr tls_socket_factory)
      : raw_socket_factory_(std::move(raw_socket_factory)),
        tls_socket_factory_(std::move(tls_socket_factory)) {}

  Network::TransportSocketPtr createDownstreamTransportSocket() const override;
  bool implementsSecureTransport() const override { return false; }

private:
  Network::DownstreamTransportSocketFactoryPtr raw_socket_factory_;
  Network::DownstreamTransportSocketFactoryPtr tls_socket_factory_;
};

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
