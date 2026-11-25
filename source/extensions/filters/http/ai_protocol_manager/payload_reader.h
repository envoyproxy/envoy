#pragma once

#include <cstdint>
#include <memory>

#include "envoy/buffer/buffer.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/semaphore.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Reference-counted read handle over a payload stored inline in memory or in an ExternalBuffer,
// serializing ExternalBuffer::read() calls across coroutines via a single-flight semaphore.
class PayloadReader : public std::enable_shared_from_this<PayloadReader> {
public:
  // Adopts `data` (draining it) into an immediately readable in-memory PayloadReader.
  static std::shared_ptr<PayloadReader> fromInMemory(Buffer::Instance& data);

  // Wraps an already-flushed `buffer` of `length` bytes in an immediately readable PayloadReader.
  static std::shared_ptr<PayloadReader> fromExternalBuffer(std::shared_ptr<ExternalBuffer> buffer,
                                                           uint64_t length);

  // Creates a PayloadReader of `length` bytes whose ExternalBuffer flush is still in flight.
  static std::shared_ptr<PayloadReader> createPendingExternal(uint64_t length);

  // Creates a PayloadReader representing an already-failed external buffer write.
  static std::shared_ptr<PayloadReader> createFailed(uint64_t length = 0);

  PayloadReader(const PayloadReader&) = delete;
  PayloadReader& operator=(const PayloadReader&) = delete;
  ~PayloadReader() = default;

  // Total byte length of the payload.
  uint64_t length() const { return total_length_; }

  // True when every byte in [0, length()) can be read immediately without waiting for an in-flight
  // external durability flush.
  bool allBytesReadable() const { return !failed_ && !pending_permit_.hasPermits(); }

  // True if external storage failed during durability handoff or on a subsequent read.
  bool hasFailed() const { return failed_; }

  // Reads `[offset, offset + max_length)` as a coroutine, suspending while an ExternalBuffer flush
  // is still in flight. Already-readable inline and zero-length reads complete synchronously
  // without suspending; ExternalBuffer reads serialize single-flight access via a 1-permit
  // semaphore so cancelling one attempt mid-read via CoroutineContext cannot overlap a second
  // ExternalBuffer::read() until the underlying callback returns.
  Coroutine::Task<absl::StatusOr<Buffer::InstancePtr>> readChunk(uint64_t offset,
                                                                 uint64_t max_length);

  // Handoff hooks invoked once a pending ExternalBuffer flush finishes.
  void completeExternalHandoff(std::shared_ptr<ExternalBuffer> buffer);
  void failExternalHandoff();

private:
  class ReadExternalBuffer;

  PayloadReader() = default;

  uint64_t total_length_{0};
  Buffer::OwnedImpl in_memory_;
  std::shared_ptr<ExternalBuffer> buffer_;
  Coroutine::SemaphorePtr read_sem_;
  Coroutine::SemaphoreReservation pending_permit_;

  bool failed_{false};
};

using PayloadReaderSharedPtr = std::shared_ptr<PayloadReader>;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
