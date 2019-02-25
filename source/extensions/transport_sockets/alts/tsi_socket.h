#pragma once

#include "envoy/network/transport_socket.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/raw_buffer_socket.h"

#include "extensions/transport_sockets/alts/noop_transport_socket_callbacks.h"
#include "extensions/transport_sockets/alts/tsi_frame_protector.h"
#include "extensions/transport_sockets/alts/tsi_handshaker.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

/**
 * A factory function to create TsiHandshaker
 * @param dispatcher the dispatcher for the thread where the socket is running on.
 * @param local_address the local address of the connection.
 * @param remote_address the remote address of the connection.
 */
typedef std::function<TsiHandshakerPtr(
    Event::Dispatcher& dispatcher, const Network::Address::InstanceConstSharedPtr& local_address,
    const Network::Address::InstanceConstSharedPtr& remote_address)>
    HandshakerFactory;

/**
 * A function to validate the peer of the connection.
 * @param peer the detail peer information of the connection.
 * @param err an error message to indicate why the peer is invalid. This is an
 * output param that should be populated by the function implementation.
 * @return true if the peer is valid or false if the peer is invalid.
 */
typedef std::function<bool(const tsi_peer& peer, std::string& err)> HandshakeValidator;

/**
 * A implementation of Network::TransportSocket based on gRPC TSI
 */
class TsiSocket : public Network::TransportSocket,
                  public TsiHandshakerCallbacks,
                  public Logger::Loggable<Logger::Id::connection> {
public:
  // For Test
  TsiSocket(HandshakerFactory handshaker_factory, HandshakeValidator handshake_validator,
            Network::TransportSocketPtr&& raw_socket_ptr);

  /**
   * @param handshaker_factory a function to initiate a TsiHandshaker
   * @param handshake_validator a function to validate the peer. Called right
   * after the handshake completed with peer data to do the peer validation.
   * The connection will be closed immediately if it returns false.
   */
  TsiSocket(HandshakerFactory handshaker_factory, HandshakeValidator handshake_validator);
  virtual ~TsiSocket();

  // Network::TransportSocket
  void setTransportSocketCallbacks(Envoy::Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  bool canFlushClose() override { return handshake_complete_; }
  const Envoy::Ssl::Connection* ssl() const override { return nullptr; }
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  void closeSocket(Network::ConnectionEvent event) override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  void onConnected() override;

  // TsiHandshakerCallbacks
  void onNextDone(NextResultPtr&& result) override;

private:
  Network::PostIoAction doHandshake();
  void doHandshakeNext();
  Network::PostIoAction doHandshakeNextDone(NextResultPtr&& next_result);

  HandshakerFactory handshaker_factory_;
  HandshakeValidator handshake_validator_;
  TsiHandshakerPtr handshaker_{};
  bool handshaker_next_calling_{};

  TsiFrameProtectorPtr frame_protector_;

  Envoy::Network::TransportSocketCallbacks* callbacks_{};
  NoOpTransportSocketCallbacksPtr noop_callbacks_;
  Network::TransportSocketPtr raw_buffer_socket_;

  Envoy::Buffer::OwnedImpl raw_read_buffer_;
  Envoy::Buffer::OwnedImpl raw_write_buffer_;
  bool handshake_complete_{};
  bool end_stream_read_{};
  bool read_error_{};
};

/**
 * An implementation of Network::TransportSocketFactory for TsiSocket
 */
class TsiSocketFactory : public Network::TransportSocketFactory {
public:
  TsiSocketFactory(HandshakerFactory handshaker_factory, HandshakeValidator handshake_validator);

  bool implementsSecureTransport() const override;
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr options) const override;

private:
  HandshakerFactory handshaker_factory_;
  HandshakeValidator handshake_validator_;
};

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
