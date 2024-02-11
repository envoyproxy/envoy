#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "source/extensions/transport_sockets/alts/alts_proxy.h"
#include "source/extensions/transport_sockets/alts/tsi_frame_protector.h"

#include "absl/types/span.h"
#include "grpcpp/channel.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

constexpr std::size_t AltsMinFrameSize = 16 * 1024;

struct AltsHandshakeResult {
  TsiFrameProtectorPtr frame_protector;
  std::string peer_identity;
  std::vector<uint8_t> unused_bytes;
};

// Manages a single ALTS handshake.
class AltsTsiHandshaker {
public:
  using OnNextDone = std::function<void(
      absl::Status status, void* handshaker, const unsigned char* bytes_to_send,
      size_t bytes_to_send_size, std::unique_ptr<AltsHandshakeResult> handshake_result)>;

  static std::unique_ptr<AltsTsiHandshaker>
  createForClient(std::shared_ptr<grpc::Channel> handshaker_service_channel);
  static std::unique_ptr<AltsTsiHandshaker>
  createForServer(std::shared_ptr<grpc::Channel> handshaker_service_channel);

  // Get the next ALTS handshake message for the peer.
  absl::Status next(void* handshaker, const unsigned char* received_bytes,
                    size_t received_bytes_size, OnNextDone on_next_done);

  // Exposed for testing purposes only.
  absl::StatusOr<std::unique_ptr<AltsHandshakeResult>>
  getHandshakeResult(const grpc::gcp::HandshakerResult& result,
                     absl::Span<const uint8_t> received_bytes, std::size_t bytes_consumed);
  static std::size_t computeMaxFrameSize(const grpc::gcp::HandshakerResult& result);

private:
  AltsTsiHandshaker(bool is_client, std::shared_ptr<grpc::Channel> handshaker_service_channel);

  const bool is_client_;
  const std::shared_ptr<grpc::Channel> handshaker_service_channel_;

  bool has_sent_initial_handshake_message_ = false;
  bool is_handshake_complete_ = false;
  std::unique_ptr<AltsProxy> alts_proxy_ = nullptr;
};

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
