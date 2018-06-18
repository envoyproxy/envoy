#include "extensions/transport_sockets/alts/tsi_handshaker.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

void TsiHandshaker::onNextDone(tsi_result status, void* user_data,
                               const unsigned char* bytes_to_send, size_t bytes_to_send_size,
                               tsi_handshaker_result* handshaker_result) {
  TsiHandshaker* handshaker = static_cast<TsiHandshaker*>(user_data);

  Buffer::InstancePtr to_send = std::make_unique<Buffer::OwnedImpl>();
  if (bytes_to_send_size > 0) {
    to_send->add(bytes_to_send, bytes_to_send_size);
  }

  auto next_result =
      new TsiHandshakerCallbacks::NextResult{status, std::move(to_send), {handshaker_result}};

  handshaker->dispatcher_.post([handshaker, next_result]() {
    TsiHandshakerCallbacks::NextResultPtr next_result_ptr{next_result};

    ASSERT(handshaker->calling_);
    handshaker->calling_ = false;

    if (handshaker->delete_on_done_) {
      handshaker->deferredDelete();
      return;
    }
    handshaker->callbacks_->onNextDone(std::move(next_result_ptr));
  });
}

TsiHandshaker::TsiHandshaker(CHandshakerPtr&& handshaker, Event::Dispatcher& dispatcher)
    : handshaker_(std::move(handshaker)), dispatcher_(dispatcher) {}

TsiHandshaker::~TsiHandshaker() { ASSERT(!calling_); }

tsi_result TsiHandshaker::next(Envoy::Buffer::Instance& received) {
  ASSERT(callbacks_);
  ASSERT(!calling_);
  calling_ = true;

  uint64_t received_size = received.length();
  const unsigned char* bytes_to_send = nullptr;
  size_t bytes_to_send_size = 0;
  tsi_handshaker_result* result = nullptr;
  tsi_result status = tsi_handshaker_next(
      handshaker_.get(), reinterpret_cast<const unsigned char*>(received.linearize(received_size)),
      received_size, &bytes_to_send, &bytes_to_send_size, &result, onNextDone, this);
  received.drain(received_size);

  if (status != TSI_ASYNC) {
    onNextDone(status, this, bytes_to_send, bytes_to_send_size, result);
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
