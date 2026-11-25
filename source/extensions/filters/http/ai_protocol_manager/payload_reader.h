#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "envoy/buffer/buffer.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/semaphore.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

class PayloadReader;
using PayloadReaderSharedPtr = std::shared_ptr<PayloadReader>;

// Reference-counted read handle over a payload stored inline in memory or in an ExternalBuffer,
// serializing ExternalBuffer::read() calls across coroutines via a single-flight semaphore.
class PayloadReader : public std::enable_shared_from_this<PayloadReader> {
public:
  // Awaitable that resolves once all bytes in [0, length()) are durable and readable (immediately
  // without suspending if already readable), or fails with DataLossError if external storage
  // failed.
  class ReadableAwaitable : public Coroutine::LeafAwaitable<absl::Status> {
  public:
    explicit ReadableAwaitable(PayloadReaderSharedPtr reader) : reader_(std::move(reader)) {}

  protected:
    std::optional<absl::Status> tryImmediate() override;
    void onStart() override;
    void onCancel() override;

  private:
    friend class PayloadReader;
    PayloadReaderSharedPtr reader_;
  };

  // Adopts `data` (draining it) into an immediately readable in-memory PayloadReader.
  static PayloadReaderSharedPtr fromInMemory(Buffer::Instance& data);

  // Wraps an already-flushed `buffer` of `length` bytes in an immediately readable PayloadReader.
  static PayloadReaderSharedPtr fromExternalBuffer(std::shared_ptr<ExternalBuffer> buffer,
                                                   uint64_t length);

  // Creates a PayloadReader of `length` bytes whose ExternalBuffer flush is still in flight.
  // `pending_owner`, if provided, keeps the ingest producer alive until completeExternalHandoff(),
  // failExternalHandoff(), or ~PayloadReader().
  static PayloadReaderSharedPtr
  createPendingExternal(uint64_t length, std::shared_ptr<void> pending_owner = nullptr);

  // Creates a PayloadReader representing an already-failed external buffer write.
  static PayloadReaderSharedPtr createFailed(uint64_t length = 0);

  PayloadReader(const PayloadReader&) = delete;
  PayloadReader& operator=(const PayloadReader&) = delete;
  ~PayloadReader() = default;

  // Total byte length of the payload.
  uint64_t length() const { return total_length_; }

  // True when every byte in [0, length()) can be read immediately without waiting for an in-flight
  // external durability flush.
  bool allBytesReadable() const { return readable_ && !failed_; }

  // True if external storage failed during durability handoff or on a subsequent read.
  bool hasFailed() const { return failed_; }

  // Returns the inline buffer when the payload is readable and stored in memory; nullptr when
  // backed by ExternalBuffer, pending flush, or failed.
  const Buffer::Instance* inMemoryBytes() const {
    return (allBytesReadable() && buffer_ == nullptr) ? &in_memory_ : nullptr;
  }

  // Suspends only while an ExternalBuffer terminal write is still in flight.
  ReadableAwaitable waitReadable() { return ReadableAwaitable(shared_from_this()); }

  // Reads `[offset, offset + max_length)` as a coroutine, awaiting `waitReadable()` first when an
  // ExternalBuffer flush is still in flight. Already-readable inline and zero-length reads complete
  // synchronously without suspending; ExternalBuffer reads serialize single-flight access via a
  // 1-permit semaphore so cancelling one attempt mid-read via CoroutineContext cannot overlap a
  // second ExternalBuffer::read() until the underlying callback returns.
  Coroutine::Task<absl::StatusOr<Buffer::InstancePtr>> readChunk(uint64_t offset,
                                                                 uint64_t max_length);

  // Handoff hooks invoked once a pending ExternalBuffer flush finishes. Returns any `pending_owner`
  // passed to createPendingExternal() so the caller can release it on its own stack after returning
  // rather than self-destructing mid-callback.
  std::shared_ptr<void> completeExternalHandoff(std::shared_ptr<ExternalBuffer> buffer);
  std::shared_ptr<void> failExternalHandoff();

private:
  class ReadExternalBuffer;

  PayloadReader() = default;

  uint64_t total_length_{0};
  Buffer::OwnedImpl in_memory_;
  std::shared_ptr<ExternalBuffer> buffer_;
  Coroutine::SemaphorePtr read_sem_;
  std::shared_ptr<void> pending_owner_;

  bool readable_{false};
  bool failed_{false};

  std::vector<ReadableAwaitable*> readable_waiters_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
