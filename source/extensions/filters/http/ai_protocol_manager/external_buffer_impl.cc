#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"

#include <memory>
#include <utility>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

InMemoryExternalBuffer::InMemoryExternalBuffer(Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {}

InMemoryExternalBuffer::~InMemoryExternalBuffer() {
  // Neutralize any completion callbacks that were posted but have not run yet.
  *alive_ = false;
}

void InMemoryExternalBuffer::write(Buffer::InstancePtr data, WriteCallback cb) {
  // The bytes only become visible to length() / read() once the (asynchronous)
  // completion callback below runs, modelling a write that is not durable until
  // acknowledged.
  dispatcher_.post([this, alive = alive_, data = std::move(data), cb = std::move(cb)]() mutable {
    if (!*alive) {
      return;
    }
    data_.move(*data);
    cb(ExternalBufferStatus::Ok);
  });
}

void InMemoryExternalBuffer::read(uint64_t offset, uint64_t length, ReadCallback cb) {
  // Reading past the acknowledged length is a programming error in the caller.
  ASSERT(offset + length <= data_.length());

  auto out = std::make_unique<Buffer::OwnedImpl>();
  // copyOut leaves the source buffer intact so the same bytes can be read
  // again (the store is append-only, reads are non-destructive).
  if (length > 0) {
    auto slice = std::make_unique<uint8_t[]>(length);
    data_.copyOut(offset, length, slice.get());
    out->add(slice.get(), length);
  }

  // No real I/O: the bytes are already in memory on the dispatcher thread, so the
  // read completes synchronously and the callback runs on-stack. The BufferManager
  // tolerates re-entrant completion -- it flattens the resulting read loop and
  // bounds the work done per event-loop iteration. A disk/remote store would
  // instead post and complete on a later iteration; both satisfy the contract.
  cb(ExternalBufferStatus::Ok, std::move(out));
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
