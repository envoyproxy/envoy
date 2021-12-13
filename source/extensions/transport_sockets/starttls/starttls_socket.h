#pragma once

#include "envoy/extensions/transport_sockets/starttls/v3/starttls.pb.h"
#include "envoy/extensions/transport_sockets/starttls/v3/starttls.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

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

private:
  // This is a proxy for wrapping the transport callback object passed from the consumer
  // Its primary purpose is to filter Connected events to ensure they only happen once per open
  class CallbackProxy : public Network::TransportSocketCallbacks {
  public:
    CallbackProxy(Network::TransportSocketCallbacks* callbacks) : parent_(callbacks) {}

    Network::IoHandle& ioHandle() override { return parent_->ioHandle(); }
    const Network::IoHandle& ioHandle() const override { return parent_->ioHandle(); }
    Network::Connection& connection() override { return parent_->connection(); }
    bool shouldDrainReadBuffer() override { return parent_->shouldDrainReadBuffer(); }
    void setTransportSocketIsReadable() override { return parent_->setTransportSocketIsReadable(); }
    void raiseEvent(Network::ConnectionEvent event) override {
      if (event == Network::ConnectionEvent::Connected) {
        // Don't send the connected event if we're already open
        if (isopen_) {
          return;
        }
        isopen_ = true;
      } else {
        isopen_ = false;
      }

      parent_->raiseEvent(event);
    }
    void flushWriteBuffer() override { parent_->flushWriteBuffer(); }

  private:
    Network::TransportSocketCallbacks* parent_;
    bool isopen_{false};
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

class StartTlsSocketFactory : public Network::TransportSocketFactory,
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
  bool usesProxyProtocolOptions() const override { return false; }

private:
  Network::TransportSocketFactoryPtr raw_socket_factory_;
  Network::TransportSocketFactoryPtr tls_socket_factory_;
};

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
