#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/grpc/async_client.h"

#include "source/extensions/transport_sockets/alts/alts_async_proxy.h"
#include "source/extensions/transport_sockets/alts/alts_proxy.h"
#include "source/extensions/transport_sockets/alts/tsi_frame_protector.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
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

  // Create a handshaker that talks to the ALTS handshaker service over a
  // blocking gRPC stream (see AltsProxy).
  static std::unique_ptr<AltsTsiHandshaker>
  createForClient(std::shared_ptr<grpc::Channel> handshaker_service_channel,
                  absl::string_view target_name = "");
  static std::unique_ptr<AltsTsiHandshaker>
  createForServer(std::shared_ptr<grpc::Channel> handshaker_service_channel);

  // Create a handshaker that talks to the ALTS handshaker service over Envoy's
  // asynchronous gRPC client (see AltsAsyncProxy), so that next() never waits
  // for the handshaker service. handshaker_client must not be null, and must
  // belong to the thread that calls next().
  static std::unique_ptr<AltsTsiHandshaker>
  createAsyncForClient(Grpc::RawAsyncClientSharedPtr handshaker_client,
                       absl::string_view target_name = "");
  static std::unique_ptr<AltsTsiHandshaker>
  createAsyncForServer(Grpc::RawAsyncClientSharedPtr handshaker_client);

  // Get the next ALTS handshake message for the peer. If this returns OK,
  // on_next_done is invoked exactly once with the result: before next() returns
  // for a blocking handshaker, and later, on the same thread, for an
  // asynchronous one (unless cancel() is called first). If this returns an
  // error, on_next_done is not invoked.
  absl::Status next(void* handshaker, const unsigned char* received_bytes,
                    size_t received_bytes_size, OnNextDone on_next_done);

  // Cancel the next() call in progress on an asynchronous handshaker, if any:
  // its on_next_done is never invoked. This must be the last call on the
  // handshaker.
  void cancel();

  // Exposed for testing purposes only.
  absl::StatusOr<std::unique_ptr<AltsHandshakeResult>>
  getHandshakeResult(const grpc::gcp::HandshakerResult& result,
                     absl::Span<const uint8_t> received_bytes, std::size_t bytes_consumed);
  static std::size_t computeMaxFrameSize(const grpc::gcp::HandshakerResult& result);

private:
  AltsTsiHandshaker(bool is_client, std::shared_ptr<grpc::Channel> handshaker_service_channel,
                    absl::string_view target_name = "");
  AltsTsiHandshaker(bool is_client, Grpc::RawAsyncClientSharedPtr handshaker_client,
                    absl::string_view target_name = "");

  absl::Status nextAsync(void* handshaker, absl::Span<const uint8_t> received_bytes,
                         OnNextDone on_next_done);

  // Turns a response of the handshaker service into the result of a next()
  // call, and passes it to on_next_done. Returns an error, without invoking
  // on_next_done, if the handshake result is invalid.
  absl::Status processResponse(const grpc::gcp::HandshakerResp& response,
                               absl::Span<const uint8_t> received_bytes, void* handshaker,
                               const OnNextDone& on_next_done);

  const bool is_client_;
  const std::shared_ptr<grpc::Channel> handshaker_service_channel_;
  const Grpc::RawAsyncClientSharedPtr handshaker_client_;
  const std::string target_name_;

  bool has_sent_initial_handshake_message_ = false;
  bool is_handshake_complete_ = false;
  std::unique_ptr<AltsProxy> alts_proxy_ = nullptr;

  // The state of an asynchronous handshaker.
  // True while a next() call waits for the handshaker service.
  bool next_in_progress_ = false;
  // A copy of the bytes received by the next() call in progress: the caller may
  // release them as soon as next() returns, and the handshake result needs them.
  std::vector<uint8_t> received_bytes_;
  // Declared last, so that it is destroyed first: its callback uses the members
  // above.
  std::unique_ptr<AltsAsyncProxy> alts_async_proxy_ = nullptr;
};

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
