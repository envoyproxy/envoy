#include "source/extensions/filters/http/ai_protocol_manager/payload_reader.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "envoy/buffer/buffer.h"

#include "source/common/common/assert.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/semaphore.h"
#include "source/common/coroutine/status_macros.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
// Bridges a single ExternalBuffer::read() call into a cancellable LeafAwaitable.
//
// ExternalBuffer::read() is single-flight and intentionally non-cancellable (e.g., backing stores
// like file or remote I/O are not easily cancelled, and keeping cancellation out of the
// ExternalBuffer interface simplifies implementations). Because an in-flight read continues past
// coroutine cancellation, onStart() moves the single-flight semaphore `permit` and a
// `PayloadReaderSharedPtr` into the read callback:
//   - `permit` stays held until the underlying read callback fires so a subsequent reader (e.g. on
//     upstream retry) cannot overlap an orphaned read, and is released before `complete()` so an
//     uncontended caller can re-acquire immediately without suspending.
//   - `reader` keeps the ExternalBuffer and `failed_` alive until the callback completes.
class PayloadReader::ReadExternalBuffer
    : public Coroutine::LeafAwaitable<absl::StatusOr<Buffer::InstancePtr>> {
public:
  ReadExternalBuffer(PayloadReaderSharedPtr reader, uint64_t offset, uint64_t max_length,
                     Coroutine::SemaphoreReservation permit)
      : reader_(std::move(reader)), offset_(offset), max_length_(max_length),
        permit_(std::move(permit)) {}

protected:
  void onStart() override {
    auto on_read = [this, alive = std::weak_ptr<bool>(alive_), reader = reader_,
                    permit = std::move(permit_)](ExternalBufferStatus status,
                                                 Buffer::InstancePtr data) mutable {
      absl::StatusOr<Buffer::InstancePtr> result;
      if (status != ExternalBufferStatus::Ok) {
        reader->failed_ = true;
        result = absl::DataLossError("external buffer read failed");
      } else {
        result = std::move(data);
      }
      permit.release();
      if (!alive.expired()) {
        complete(std::move(result));
      }
    };
    reader_->buffer_->read(offset_, max_length_, std::move(on_read));
  }

  void onCancel() override { alive_.reset(); }

private:
  PayloadReaderSharedPtr reader_;
  uint64_t offset_;
  uint64_t max_length_;
  Coroutine::SemaphoreReservation permit_;
  std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

std::optional<absl::Status> PayloadReader::ReadableAwaitable::tryImmediate() {
  if (reader_->failed_) {
    return absl::DataLossError("external buffer write failed");
  }
  if (reader_->readable_) {
    return absl::OkStatus();
  }
  return std::nullopt;
}

void PayloadReader::ReadableAwaitable::onStart() {
  ASSERT(!reader_->failed_ && !reader_->readable_);
  reader_->readable_waiters_.push_back(this);
}

void PayloadReader::ReadableAwaitable::onCancel() { std::erase(reader_->readable_waiters_, this); }

PayloadReaderSharedPtr PayloadReader::fromInMemory(Buffer::Instance& data) {
  PayloadReaderSharedPtr reader(new PayloadReader());
  reader->total_length_ = data.length();
  reader->in_memory_.move(data);
  reader->readable_ = true;
  return reader;
}

PayloadReaderSharedPtr PayloadReader::fromExternalBuffer(std::shared_ptr<ExternalBuffer> buffer,
                                                         uint64_t length) {
  ASSERT(buffer != nullptr);
  PayloadReaderSharedPtr reader(new PayloadReader());
  reader->total_length_ = length;
  reader->buffer_ = std::move(buffer);
  reader->read_sem_ = std::make_shared<Coroutine::Semaphore>(1);
  reader->readable_ = true;
  return reader;
}

PayloadReaderSharedPtr PayloadReader::createPendingExternal(uint64_t length) {
  PayloadReaderSharedPtr reader(new PayloadReader());
  reader->total_length_ = length;
  reader->read_sem_ = std::make_shared<Coroutine::Semaphore>(1);
  return reader;
}

PayloadReaderSharedPtr PayloadReader::createFailed(uint64_t length) {
  PayloadReaderSharedPtr reader(new PayloadReader());
  reader->total_length_ = length;
  reader->failed_ = true;
  return reader;
}

Coroutine::Task<absl::StatusOr<Buffer::InstancePtr>> PayloadReader::readChunk(uint64_t offset,
                                                                              uint64_t max_length) {
  PayloadReaderSharedPtr self = shared_from_this();
  CO_RETURN_IF_ERROR(co_await self->waitReadable());
  if (offset > self->total_length_ || max_length > self->total_length_ - offset) {
    co_return absl::InvalidArgumentError("payload read range out of bounds");
  }
  if (self->buffer_ == nullptr || max_length == 0) {
    auto data = std::make_unique<Buffer::OwnedImpl>();
    if (max_length > 0) {
      auto bytes = std::make_unique<uint8_t[]>(max_length);
      self->in_memory_.copyOut(offset, max_length, bytes.get());
      data->add(bytes.get(), max_length);
    }
    co_return data;
  }

  ASSIGN_OR_CO_RETURN(Coroutine::SemaphoreReservation permit, co_await self->read_sem_->acquire(1));
  if (self->failed_) {
    co_return absl::DataLossError("external buffer failed");
  }

  co_return co_await ReadExternalBuffer(self, offset, max_length, std::move(permit));
}

void PayloadReader::completeExternalHandoff(std::shared_ptr<ExternalBuffer> buffer) {
  if (failed_ || readable_) {
    return;
  }
  ASSERT(buffer != nullptr);
  buffer_ = std::move(buffer);
  readable_ = true;
  while (!readable_waiters_.empty()) {
    ReadableAwaitable* waiter = readable_waiters_.front();
    readable_waiters_.erase(readable_waiters_.begin());
    waiter->complete(absl::OkStatus());
  }
}

void PayloadReader::failExternalHandoff() {
  if (failed_ || readable_) {
    return;
  }
  failed_ = true;
  while (!readable_waiters_.empty()) {
    ReadableAwaitable* waiter = readable_waiters_.front();
    readable_waiters_.erase(readable_waiters_.begin());
    waiter->complete(absl::DataLossError("external buffer write failed"));
  }
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
