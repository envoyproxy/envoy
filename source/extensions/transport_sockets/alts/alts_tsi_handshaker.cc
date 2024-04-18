#include "source/extensions/transport_sockets/alts/alts_tsi_handshaker.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

using ::grpc::gcp::HandshakerResp;

constexpr std::size_t AltsAes128GcmRekeyKeyLength = 44;

std::unique_ptr<AltsTsiHandshaker>
AltsTsiHandshaker::createForClient(std::shared_ptr<grpc::Channel> handshaker_service_channel) {
  return absl::WrapUnique(new AltsTsiHandshaker(/*is_client=*/true, handshaker_service_channel));
}

std::unique_ptr<AltsTsiHandshaker>
AltsTsiHandshaker::createForServer(std::shared_ptr<grpc::Channel> handshaker_service_channel) {
  return absl::WrapUnique(new AltsTsiHandshaker(/*is_client=*/false, handshaker_service_channel));
}

AltsTsiHandshaker::AltsTsiHandshaker(bool is_client,
                                     std::shared_ptr<grpc::Channel> handshaker_service_channel)
    : is_client_(is_client), handshaker_service_channel_(handshaker_service_channel) {}

absl::Status AltsTsiHandshaker::next(void* handshaker, const unsigned char* received_bytes,
                                     size_t received_bytes_size, OnNextDone on_next_done) {
  // Argument and state checks.
  if (handshaker == nullptr || (received_bytes == nullptr && received_bytes_size > 0) ||
      on_next_done == nullptr) {
    return absl::InvalidArgumentError("Invalid nullptr argument to AltsTsiHandshaker::Next.");
  }
  if (is_handshake_complete_) {
    return absl::InternalError("Handshake is already complete.");
  }

  // Get a handshake message from the handshaker service.
  absl::Span<const uint8_t> in_bytes = absl::MakeConstSpan(received_bytes, received_bytes_size);
  HandshakerResp response;
  if (!has_sent_initial_handshake_message_) {
    has_sent_initial_handshake_message_ = true;
    auto alts_proxy = AltsProxy::create(handshaker_service_channel_);
    if (!alts_proxy.ok()) {
      return alts_proxy.status();
    }
    alts_proxy_ = *std::move(alts_proxy);
    if (is_client_) {
      auto client_start = alts_proxy_->sendStartClientHandshakeReq();
      if (!client_start.ok()) {
        return client_start.status();
      }
      response = *std::move(client_start);
    } else {
      auto server_start = alts_proxy_->sendStartServerHandshakeReq(in_bytes);
      if (!server_start.ok()) {
        return server_start.status();
      }
      response = *std::move(server_start);
    }
  } else {
    auto next = alts_proxy_->sendNextHandshakeReq(in_bytes);
    if (!next.ok()) {
      return next.status();
    }
    response = *std::move(next);
  }

  // Maybe prepare the handshake result.
  std::unique_ptr<AltsHandshakeResult> handshake_result = nullptr;
  if (response.has_result()) {
    is_handshake_complete_ = true;
    auto result = getHandshakeResult(response.result(), in_bytes, response.bytes_consumed());
    if (!result.ok()) {
      return result.status();
    }
    handshake_result = *std::move(result);
  }

  // Write the out bytes.
  const std::string& out_bytes = response.out_frames();
  on_next_done(absl::OkStatus(), handshaker,
               reinterpret_cast<const unsigned char*>(out_bytes.c_str()), out_bytes.size(),
               std::move(handshake_result));
  return absl::OkStatus();
}

std::size_t AltsTsiHandshaker::computeMaxFrameSize(const grpc::gcp::HandshakerResult& result) {
  if (result.max_frame_size() > 0) {
    return std::clamp(static_cast<std::size_t>(result.max_frame_size()), AltsMinFrameSize,
                      MaxFrameSize);
  }
  return AltsMinFrameSize;
}

absl::StatusOr<std::unique_ptr<AltsHandshakeResult>>
AltsTsiHandshaker::getHandshakeResult(const grpc::gcp::HandshakerResult& result,
                                      absl::Span<const uint8_t> received_bytes,
                                      std::size_t bytes_consumed) {
  // Validate the HandshakerResult message.
  if (!result.has_peer_identity()) {
    return absl::FailedPreconditionError("Handshake result is missing peer identity.");
  }
  if (!result.has_local_identity()) {
    return absl::FailedPreconditionError("Handshake result is missing local identity.");
  }
  if (!result.has_peer_rpc_versions()) {
    return absl::FailedPreconditionError("Handshake result is missing peer rpc versions.");
  }
  if (result.application_protocol().empty()) {
    return absl::FailedPreconditionError("Handshake result has empty application protocol.");
  }
  if (result.record_protocol() != RecordProtocol) {
    return absl::FailedPreconditionError(
        "Handshake result's record protocol is not ALTSRP_GCM_AES128_REKEY.");
  }
  if (result.key_data().size() < AltsAes128GcmRekeyKeyLength) {
    return absl::FailedPreconditionError("Handshake result's key data is too short.");
  }
  if (bytes_consumed > received_bytes.size()) {
    return absl::FailedPreconditionError(
        "Handshaker service consumed more bytes than were received from the "
        "peer.");
  }

  // Create the frame protector.
  std::size_t max_frame_size = computeMaxFrameSize(result);
  tsi_zero_copy_grpc_protector* protector = nullptr;
  grpc_core::ExecCtx exec_ctx;
  tsi_result ok = alts_zero_copy_grpc_protector_create(
      reinterpret_cast<const uint8_t*>(result.key_data().data()), AltsAes128GcmRekeyKeyLength, true,
      is_client_,
      /*is_integrity_only=*/false, /*enable_extra_copy=*/false, &max_frame_size, &protector);
  if (ok != TSI_OK) {
    return absl::InternalError(absl::StrFormat("Failed to create frame protector: %zu", ok));
  }

  // Calculate the unused bytes.
  std::size_t unused_bytes_size = received_bytes.size() - bytes_consumed;
  const uint8_t* unused_bytes_ptr = received_bytes.data() + bytes_consumed;
  std::vector<uint8_t> unused_bytes(unused_bytes_ptr, unused_bytes_ptr + unused_bytes_size);

  // Create and return the AltsHandshakeResult.
  auto handshake_result = std::make_unique<AltsHandshakeResult>();
  handshake_result->frame_protector = std::make_unique<TsiFrameProtector>(protector);
  handshake_result->peer_identity = result.peer_identity().service_account();
  handshake_result->unused_bytes = unused_bytes;
  return handshake_result;
}

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
