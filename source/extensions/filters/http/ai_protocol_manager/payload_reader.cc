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
namespace {

class ExternalReadAwaitable : public Coroutine::LeafAwaitable<absl::StatusOr<Buffer::InstancePtr>> {
public:
  ExternalReadAwaitable(PayloadReaderSharedPtr reader, ExternalBuffer& buffer, bool& failed,
                        uint64_t offset, uint64_t max_length,
                        Coroutine::SemaphoreReservation permit)
      : reader_(std::move(reader)), buffer_(buffer), failed_(failed), offset_(offset),
        max_length_(max_length), permit_(std::move(permit)) {}

protected:
  void onStart() override {
    buffer_.read(offset_, max_length_,
                 [self = self_, reader = reader_, &failed = failed_, permit = std::move(permit_)](
                     ExternalBufferStatus status, Buffer::InstancePtr data) mutable {
                   // `reader` keeps `*reader` (and therefore `failed`) alive across cancellation.
                   // Release the single-flight permit before resuming so the next chunk or a
                   // waiting coroutine can immediately acquire it; if this coroutine's context was
                   // cancelled mid-read (`*self == nullptr`), the permit stayed held until this
                   // underlying ExternalBuffer::read() callback completed.
                   if (status != ExternalBufferStatus::Ok) {
                     failed = true;
                     permit.release();
                     if (ExternalReadAwaitable* awaitable = std::exchange(*self, nullptr);
                         awaitable != nullptr) {
                       awaitable->complete(absl::DataLossError("external buffer read failed"));
                     }
                     return;
                   }
                   permit.release();
                   if (ExternalReadAwaitable* awaitable = std::exchange(*self, nullptr);
                       awaitable != nullptr) {
                     awaitable->complete(std::move(data));
                   }
                 });
  }

  void onCancel() override { *self_ = nullptr; }

private:
  PayloadReaderSharedPtr reader_;
  ExternalBuffer& buffer_;
  bool& failed_;
  uint64_t offset_;
  uint64_t max_length_;
  Coroutine::SemaphoreReservation permit_;
  std::shared_ptr<ExternalReadAwaitable*> self_{std::make_shared<ExternalReadAwaitable*>(this)};
};

} // namespace

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

  std::optional<Coroutine::SemaphoreReservation> permit = self->read_sem_->tryAcquire(1);
  if (!permit.has_value()) {
    ASSIGN_OR_CO_RETURN(permit, co_await self->read_sem_->acquire(1));
  }
  if (self->failed_) {
    co_return absl::DataLossError("external buffer failed");
  }

  co_return co_await ExternalReadAwaitable(self, *self->buffer_, self->failed_, offset, max_length,
                                           std::move(*permit));
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
