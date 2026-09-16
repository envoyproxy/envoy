#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "envoy/buffer/buffer.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Awaitable that writes one span of output through the BufferManager and resumes its coroutine
// when that write has drained.
//
// Output on this path is produced in pieces -- a JSON node here, a run of bytes replayed from the
// external buffer there -- but BufferManager permits only one replay in flight. Awaiting each
// piece is what serializes them: a producing coroutine suspends until the buffer has taken the
// bytes, which both upholds that invariant and makes the downstream filter chain's backpressure
// the producer's own, so a slow reader throttles serialization instead of accumulating output in
// memory.
//
// Two sources of bytes are supported: a range already durable in the external buffer (replayed
// without materializing it) and a buffer of the caller's own bytes (moved in, so the caller's
// buffer is drained).
class ReplayAwaitable : public Coroutine::LeafAwaitable<absl::Status> {
public:
  // Replays `length` bytes at `offset` in the external buffer.
  ReplayAwaitable(BufferManager& buffer_manager, uint64_t offset, uint64_t length)
      : buffer_manager_(buffer_manager), offset_(offset), length_(length), is_injected_(false) {}

  // Writes out `data`, which is moved from and left empty.
  ReplayAwaitable(BufferManager& buffer_manager, Buffer::Instance& data)
      : buffer_manager_(buffer_manager), is_injected_(true) {
    data_.move(data);
  }

protected:
  // Fast path: complete immediately without suspending if nothing needs to be replayed.
  std::optional<absl::Status> tryImmediate() override {
    if ((is_injected_ && data_.length() == 0) || (!is_injected_ && length_ == 0)) {
      return absl::OkStatus();
    }
    if (!is_injected_ &&
        (offset_ > buffer_manager_.length() || length_ > buffer_manager_.length() - offset_)) {
      return absl::InvalidArgumentError("replay range exceeds buffer length");
    }
    return std::nullopt;
  }

  void onStart() override {
    if (is_injected_) {
      buffer_manager_.inject(data_, [this](absl::Status status) { complete(std::move(status)); });
    } else {
      buffer_manager_.replay(offset_, length_,
                             [this](absl::Status status) { complete(std::move(status)); });
    }
  }

  void onCancel() override { buffer_manager_.cancelReplay(); }

private:
  BufferManager& buffer_manager_;
  std::shared_ptr<BufferManager> manager_lifetime_{buffer_manager_.weak_from_this().lock()};
  uint64_t offset_{0};
  uint64_t length_{0};
  bool is_injected_{false};
  Buffer::OwnedImpl data_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
