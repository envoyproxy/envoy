#include "source/extensions/transport_sockets/alts/tsi_handshaker.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/extensions/transport_sockets/alts/alts_tsi_handshaker.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

void TsiHandshaker::onNextDone(absl::Status status, void* handshaker,
                               const unsigned char* bytes_to_send, size_t bytes_to_send_size,
                               std::unique_ptr<AltsHandshakeResult> handshake_result) {
  TsiHandshaker* tsi_handshaker = static_cast<TsiHandshaker*>(handshaker);
  Buffer::InstancePtr to_send = std::make_unique<Buffer::OwnedImpl>();
  if (bytes_to_send_size > 0) {
    to_send->add(bytes_to_send, bytes_to_send_size);
  }

  auto next_result = new TsiHandshakerCallbacks::NextResult{status, std::move(to_send),
                                                            std::move(handshake_result)};

  tsi_handshaker->dispatcher_.post([tsi_handshaker, next_result]() {
    TsiHandshakerCallbacks::NextResultPtr next_result_ptr{next_result};

    ASSERT(tsi_handshaker->calling_);
    tsi_handshaker->calling_ = false;

    if (tsi_handshaker->delete_on_done_) {
      tsi_handshaker->deferredDelete();
      return;
    }
    tsi_handshaker->callbacks_->onNextDone(std::move(next_result_ptr));
  });
}

TsiHandshaker::TsiHandshaker(std::unique_ptr<AltsTsiHandshaker> handshaker,
                             Event::Dispatcher& dispatcher)
    : handshaker_(std::move(handshaker)), dispatcher_(dispatcher) {}

TsiHandshaker::~TsiHandshaker() { ASSERT(!calling_); }

absl::Status TsiHandshaker::next(Envoy::Buffer::Instance& received) {
  ASSERT(callbacks_);
  ASSERT(!calling_);
  calling_ = true;

  uint64_t received_size = received.length();
  absl::Status status = handshaker_->next(
      this, reinterpret_cast<const unsigned char*>(received.linearize(received_size)),
      received_size, onNextDone);

  received.drain(received_size);
  if (!status.ok()) {
    onNextDone(status, this, /*bytes_to_send=*/nullptr,
               /*bytes_to_send_size=*/0, /*handshake_result=*/nullptr);
  }
  return status;
}

void TsiHandshaker::deferredDelete() {
  if (calling_) {
    delete_on_done_ = true;
  } else {
    dispatcher_.deferredDelete(Event::DeferredDeletablePtr{this});
  }
}

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
