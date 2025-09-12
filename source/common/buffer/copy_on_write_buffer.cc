#include "source/common/buffer/copy_on_write_buffer.h"

#include <algorithm>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"

namespace Envoy {
namespace Buffer {

CopyOnWriteBuffer::CopyOnWriteBuffer(SharedBufferPtr shared_buffer)
    : shared_buffer_(std::move(shared_buffer)) {
  ASSERT(shared_buffer_ != nullptr);
}

CopyOnWriteBuffer::CopyOnWriteBuffer(std::unique_ptr<Instance> source_buffer)
    : shared_buffer_(std::make_shared<SharedBuffer>(std::move(source_buffer))) {}

CopyOnWriteBuffer::CopyOnWriteBuffer(const CopyOnWriteBuffer& other) {
  if (other.shared_buffer_) {
    shared_buffer_ = other.shared_buffer_;
  } else if (other.private_buffer_) {
    private_buffer_ = std::make_unique<OwnedImpl>(*other.private_buffer_);
  }
}

CopyOnWriteBuffer& CopyOnWriteBuffer::operator=(const CopyOnWriteBuffer& other) {
  if (this != &other) {
    shared_buffer_.reset();
    private_buffer_.reset();
    if (other.shared_buffer_) {
      shared_buffer_ = other.shared_buffer_;
    } else if (other.private_buffer_) {
      private_buffer_ = std::make_unique<OwnedImpl>(*other.private_buffer_);
    }
  }
  return *this;
}

CopyOnWriteBuffer::~CopyOnWriteBuffer() = default;

void CopyOnWriteBuffer::ensurePrivateBuffer() {
  if (private_buffer_) {
    return;
  }
  if (shared_buffer_) {
    private_buffer_ = std::make_unique<OwnedImpl>(shared_buffer_->buffer());
    shared_buffer_.reset();
  } else {
    private_buffer_ = std::make_unique<OwnedImpl>();
  }
}

uint64_t CopyOnWriteBuffer::length() const {
  if (private_buffer_) {
    return private_buffer_->length();
  }
  if (shared_buffer_) {
    return shared_buffer_->buffer().length();
  }
  return 0;
}

void* CopyOnWriteBuffer::linearize(uint32_t size) {
  ensurePrivateBuffer();
  return private_buffer_->linearize(size);
}

void CopyOnWriteBuffer::move(Instance& rhs) {
  ensurePrivateBuffer();
  private_buffer_->move(rhs);
}

void CopyOnWriteBuffer::move(Instance& rhs, uint64_t length) {
  ensurePrivateBuffer();
  private_buffer_->move(rhs, length);
}

void CopyOnWriteBuffer::move(Instance& rhs, uint64_t length,
                             bool reset_drain_trackers_and_accounting) {
  ensurePrivateBuffer();
  private_buffer_->move(rhs, length, reset_drain_trackers_and_accounting);
}

ssize_t CopyOnWriteBuffer::search(const void* data, uint64_t size, size_t start,
                                  size_t length) const {
  if (private_buffer_) {
    return private_buffer_->search(data, size, start, length);
  }
  if (shared_buffer_) {
    return shared_buffer_->buffer().search(data, size, start, length);
  }
  return -1;
}

bool CopyOnWriteBuffer::startsWith(absl::string_view data) const {
  if (private_buffer_) {
    return private_buffer_->startsWith(data);
  }
  if (shared_buffer_) {
    return shared_buffer_->buffer().startsWith(data);
  }
  return false;
}

std::string CopyOnWriteBuffer::toString() const {
  if (private_buffer_) {
    return private_buffer_->toString();
  }
  if (shared_buffer_) {
    return shared_buffer_->buffer().toString();
  }
  return "";
}

void CopyOnWriteBuffer::copyOut(size_t start, uint64_t size, void* data) const {
  if (private_buffer_) {
    private_buffer_->copyOut(start, size, data);
  } else if (shared_buffer_) {
    shared_buffer_->buffer().copyOut(start, size, data);
  }
}

uint64_t CopyOnWriteBuffer::copyOutToSlices(uint64_t size, RawSlice* slices,
                                            uint64_t num_slice) const {
  if (private_buffer_) {
    return private_buffer_->copyOutToSlices(size, slices, num_slice);
  }
  if (shared_buffer_) {
    return shared_buffer_->buffer().copyOutToSlices(size, slices, num_slice);
  }
  return 0;
}

RawSliceVector CopyOnWriteBuffer::getRawSlices(absl::optional<uint64_t> max_slices) const {
  if (private_buffer_) {
    return private_buffer_->getRawSlices(max_slices);
  }
  if (shared_buffer_) {
    return shared_buffer_->buffer().getRawSlices(max_slices);
  }
  return {};
}

RawSlice CopyOnWriteBuffer::frontSlice() const {
  if (private_buffer_) {
    return private_buffer_->frontSlice();
  }
  if (shared_buffer_) {
    return shared_buffer_->buffer().frontSlice();
  }
  return {nullptr, 0};
}

Buffer::SliceDataPtr CopyOnWriteBuffer::extractMutableFrontSlice() {
  ensurePrivateBuffer();
  return private_buffer_->extractMutableFrontSlice();
}

void CopyOnWriteBuffer::add(const void* data, uint64_t size) {
  ensurePrivateBuffer();
  private_buffer_->add(data, size);
}

void CopyOnWriteBuffer::addBufferFragment(BufferFragment& fragment) {
  ensurePrivateBuffer();
  private_buffer_->addBufferFragment(fragment);
}

void CopyOnWriteBuffer::add(absl::string_view data) {
  ensurePrivateBuffer();
  private_buffer_->add(data);
}

void CopyOnWriteBuffer::add(const Instance& data) {
  ensurePrivateBuffer();
  private_buffer_->add(data);
}

void CopyOnWriteBuffer::prepend(absl::string_view data) {
  ensurePrivateBuffer();
  private_buffer_->prepend(data);
}

void CopyOnWriteBuffer::prepend(Instance& data) {
  ensurePrivateBuffer();
  private_buffer_->prepend(data);
}

void CopyOnWriteBuffer::drain(uint64_t size) {
  ensurePrivateBuffer();
  private_buffer_->drain(size);
}

void CopyOnWriteBuffer::addDrainTracker(std::function<void()> drain_tracker) {
  ensurePrivateBuffer();
  private_buffer_->addDrainTracker(std::move(drain_tracker));
}

Buffer::Reservation CopyOnWriteBuffer::reserveForRead() {
  ensurePrivateBuffer();
  return private_buffer_->reserveForRead();
}

Buffer::ReservationSingleSlice CopyOnWriteBuffer::reserveSingleSlice(uint64_t length,
                                                                     bool separate_slice) {
  ensurePrivateBuffer();
  return private_buffer_->reserveSingleSlice(length, separate_slice);
}

size_t CopyOnWriteBuffer::addFragments(absl::Span<const absl::string_view> fragments) {
  ensurePrivateBuffer();
  return private_buffer_->addFragments(fragments);
}

void CopyOnWriteBuffer::commit(uint64_t length, absl::Span<RawSlice> slices,
                               ReservationSlicesOwnerPtr) {
  if (length == 0 || slices.empty()) {
    return;
  }
  ensurePrivateBuffer();
  for (size_t i = 0; i < slices.size() && length > 0; ++i) {
    const auto& slice = slices[i];
    const uint64_t to_add = std::min(length, static_cast<uint64_t>(slice.len_));
    if (to_add > 0) {
      private_buffer_->add(slice.mem_, to_add);
      length -= to_add;
    }
  }
}

std::unique_ptr<CopyOnWriteBuffer>
createCopyOnWriteBuffer(std::unique_ptr<Instance> source_buffer) {
  return std::make_unique<CopyOnWriteBuffer>(std::move(source_buffer));
}

std::vector<std::unique_ptr<CopyOnWriteBuffer>>
createSharedCopyOnWriteBuffers(std::unique_ptr<Instance> source_buffer, size_t count) {
  auto shared_buffer = std::make_shared<SharedBuffer>(std::move(source_buffer));
  std::vector<std::unique_ptr<CopyOnWriteBuffer>> result;
  result.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    result.emplace_back(std::make_unique<CopyOnWriteBuffer>(shared_buffer));
  }
  return result;
}

} // namespace Buffer
} // namespace Envoy
