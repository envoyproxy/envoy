#pragma once

#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/watermark_buffer.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/extensions/transport_sockets/alts/noop_transport_socket_callbacks.h"
#include "source/extensions/transport_sockets/alts/tsi_frame_protector.h"
#include "source/extensions/transport_sockets/alts/tsi_handshaker.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

struct TsiInfo {
  std::string peer_identity_;
};

/**
 * A factory function to create TsiHandshaker
 * @param dispatcher the dispatcher for the thread where the socket is running on.
 * @param local_address the local address of the connection.
 * @param remote_address the remote address of the connection.
 */
using HandshakerFactory = std::function<TsiHandshakerPtr(
    Event::Dispatcher& dispatcher, const Network::Address::InstanceConstSharedPtr& local_address,
    const Network::Address::InstanceConstSharedPtr& remote_address)>;

/**
 * A function to validate the peer of the connection.
 * @param err an error message to indicate why the peer is invalid. This is an
 * output param that should be populated by the function implementation.
 * @return true if the peer is valid or false if the peer is invalid.
 */
using HandshakeValidator = std::function<bool(TsiInfo& tsi_info, std::string& err)>;

/* Forward declaration */
class TsiTransportSocketCallbacks;

/**
 * A implementation of Network::TransportSocket based on gRPC TSI
 */
class TsiSocket : public Network::TransportSocket,
                  public TsiHandshakerCallbacks,
                  public Logger::Loggable<Logger::Id::connection> {
public:
  // For Test
  TsiSocket(HandshakerFactory handshaker_factory, HandshakeValidator handshake_validator,
            Network::TransportSocketPtr&& raw_socket_ptr, bool downstream);

  /**
   * @param handshaker_factory a function to initiate a TsiHandshaker
   * @param handshake_validator a function to validate the peer. Called right
   * after the handshake completed with peer data to do the peer validation.
   * The connection will be closed immediately if it returns false.
   * @param downstream is true for downstream transport socket.
   */
  TsiSocket(HandshakerFactory handshaker_factory, HandshakeValidator handshake_validator,
            bool downstream);
  ~TsiSocket() override;

  // Network::TransportSocket
  void setTransportSocketCallbacks(Envoy::Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  absl::string_view failureReason() const override;
  bool canFlushClose() override { return handshake_complete_; }
  Envoy::Ssl::ConnectionInfoConstSharedPtr ssl() const override { return nullptr; }
  bool startSecureTransport() override { return false; }
  void configureInitialCongestionWindow(uint64_t, std::chrono::microseconds) override {}
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  void closeSocket(Network::ConnectionEvent event) override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  void onConnected() override;

  // TsiHandshakerCallbacks
  void onNextDone(NextResultPtr&& result) override;

  // This API should be called only after ALTS handshake finishes successfully.
  size_t actualFrameSizeToUse() { return actual_frame_size_to_use_; }
  // Set actual_frame_size_to_use_. Exposed for testing purpose.
  void setActualFrameSizeToUse(size_t frame_size) { actual_frame_size_to_use_ = frame_size; }
  // Set frame_overhead_size_. Exposed for testing purpose.
  void setFrameOverheadSize(size_t overhead_size) { frame_overhead_size_ = overhead_size; }

private:
  Network::PostIoAction doHandshake();
  Network::PostIoAction doHandshakeNext();
  Network::PostIoAction doHandshakeNextDone(NextResultPtr&& next_result);

  // Helper function to perform repeated read and unprotect operations.
  Network::IoResult repeatReadAndUnprotect(Buffer::Instance& buffer, Network::IoResult prev_result);
  // Helper function to perform repeated protect and write operations.
  Network::IoResult repeatProtectAndWrite(Buffer::Instance& buffer, bool end_stream);
  // Helper function to read from a raw socket and update status.
  Network::IoResult readFromRawSocket();

  HandshakerFactory handshaker_factory_;
  HandshakeValidator handshake_validator_;
  TsiHandshakerPtr handshaker_{};
  bool handshaker_next_calling_{};

  TsiFrameProtectorPtr frame_protector_;
  // default_max_frame_size_ is the maximum frame size supported by
  // TsiSocket.
  size_t default_max_frame_size_{16384};
  // actual_frame_size_to_use_ is the actual frame size used by
  // frame protector, which is the result of frame size negotiation.
  size_t actual_frame_size_to_use_{0};
  // frame_overhead_size_ includes 4 bytes frame message type and 16 bytes tag length.
  // It is consistent with gRPC ALTS zero copy frame protector implementation.
  // The maximum size of data that can be protected for each frame is equal to
  // actual_frame_size_to_use_ - frame_overhead_size_.
  size_t frame_overhead_size_{20};

  Envoy::Network::TransportSocketCallbacks* callbacks_{};
  std::unique_ptr<TsiTransportSocketCallbacks> tsi_callbacks_;
  Network::TransportSocketPtr raw_buffer_socket_;
  const bool downstream_;

  Buffer::WatermarkBuffer raw_read_buffer_{[]() {}, []() {}, []() {}};
  Envoy::Buffer::OwnedImpl raw_write_buffer_;
  bool handshake_complete_{};
  bool end_stream_read_{};
  bool read_error_{};
  uint64_t prev_bytes_to_drain_{};
};

/**
 * An implementation of Network::UpstreamTransportSocketFactory for TsiSocket
 */
class TsiSocketFactory : public Network::DownstreamTransportSocketFactory,
                         public Network::CommonUpstreamTransportSocketFactory {
public:
  TsiSocketFactory(HandshakerFactory handshaker_factory, HandshakeValidator handshake_validator);

  bool implementsSecureTransport() const override;
  absl::string_view defaultServerNameIndication() const override { return ""; }

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        Upstream::HostDescriptionConstSharedPtr) const override;

  Network::TransportSocketPtr createDownstreamTransportSocket() const override;

private:
  HandshakerFactory handshaker_factory_;
  HandshakeValidator handshake_validator_;
};

/**
 * An implementation of Network::TransportSocketCallbacks for TsiSocket
 */
class TsiTransportSocketCallbacks : public NoOpTransportSocketCallbacks {
public:
  TsiTransportSocketCallbacks(Network::TransportSocketCallbacks& parent,
                              const Buffer::WatermarkBuffer& read_buffer)
      : NoOpTransportSocketCallbacks(parent), raw_read_buffer_(read_buffer) {}
  bool shouldDrainReadBuffer() override {
    return raw_read_buffer_.length() >= raw_read_buffer_.highWatermark();
  }

private:
  const Buffer::WatermarkBuffer& raw_read_buffer_;
};

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
