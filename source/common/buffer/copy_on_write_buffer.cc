#include "source/common/buffer/copy_on_write_buffer.h"

#include <algorithm>

#include "source/common/common/assert.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Buffer {

CopyOnWriteBuffer::CopyOnWriteBuffer(SharedBufferPtr shared_buffer)
    : shared_buffer_(std::move(shared_buffer)) {
  ASSERT(shared_buffer_ != nullptr);
}

CopyOnWriteBuffer::CopyOnWriteBuffer(std::unique_ptr<Instance> source_buffer)
    : shared_buffer_(std::make_shared<SharedBuffer>(std::move(source_buffer))) {
  ASSERT(shared_buffer_ != nullptr);
  // Note: SharedBuffer constructor already sets ref_count to 1, so we don't need addRef() here.
}

CopyOnWriteBuffer::CopyOnWriteBuffer(const CopyOnWriteBuffer& other) {
  if (other.shared_buffer_) {
    shared_buffer_ = other.shared_buffer_;
  } else if (other.private_buffer_) {
    // If other has a private buffer, create a new one by copying.
    private_buffer_ = std::make_unique<OwnedImpl>(*other.private_buffer_);
  }
  account_ = other.account_;
}

CopyOnWriteBuffer& CopyOnWriteBuffer::operator=(const CopyOnWriteBuffer& other) {
  if (this != &other) {
    // Release current buffers as shared_ptr will handle reference counting.
    shared_buffer_.reset();
    private_buffer_.reset();

    // Copy from other.
    if (other.shared_buffer_) {
      shared_buffer_ = other.shared_buffer_;
    } else if (other.private_buffer_) {
      private_buffer_ = std::make_unique<OwnedImpl>(*other.private_buffer_);
    }
    account_ = other.account_;
  }
  return *this;
}

CopyOnWriteBuffer::~CopyOnWriteBuffer() {
  // shared_ptr should automatically handle the cleanup.
}

void CopyOnWriteBuffer::ensurePrivateBuffer() {
  if (private_buffer_) {
    // Already have a private buffer.
    return;
  }

  if (shared_buffer_) {
    if (shared_buffer_.use_count() == 1) {
      // We're the only reference, we can take exclusive ownership.
      private_buffer_ = std::make_unique<OwnedImpl>(shared_buffer_->buffer());
      shared_buffer_.reset();
    } else {
      // Multiple references exist, we need to copy.
      ENVOY_LOG_MISC(debug, "Copy-on-write buffer triggering copy-on-write for {} byte buffer",
                     shared_buffer_->buffer().length());
      private_buffer_ = std::make_unique<OwnedImpl>(shared_buffer_->buffer());
      // Don't bind account to non-empty buffer since OwnedImpl requires empty buffers.
      shared_buffer_.reset();
    }
  } else {
    // No shared buffer, create an empty private buffer.
    private_buffer_ = std::make_unique<OwnedImpl>();
    if (account_) {
      private_buffer_->bindAccount(account_);
    }
  }
}

bool CopyOnWriteBuffer::isShared() const {
  return shared_buffer_ && shared_buffer_.use_count() > 1;
}

uint32_t CopyOnWriteBuffer::referenceCount() const {
  return shared_buffer_ ? shared_buffer_.use_count() : (private_buffer_ ? 1 : 0);
}

// Read operations - delegate to appropriate buffer.
uint64_t CopyOnWriteBuffer::length() const {
  if (private_buffer_) {
    return private_buffer_->length();
  }
  if (shared_buffer_) {
    return shared_buffer_->buffer().length();
  }
  return 0;
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

// Write operations - trigger copy-on-write.
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

void CopyOnWriteBuffer::bindAccount(BufferMemoryAccountSharedPtr account) {
  account_ = std::move(account);
  // Only bind the account to the private buffer if it's empty.
  // This matches the OwnedImpl constraint that bindAccount should only be called on empty buffers.
  if (private_buffer_ && private_buffer_->length() == 0) {
    private_buffer_->bindAccount(account_);
  }
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

SliceDataPtr CopyOnWriteBuffer::extractMutableFrontSlice() {
  ensurePrivateBuffer();
  return private_buffer_->extractMutableFrontSlice();
}

Reservation CopyOnWriteBuffer::reserveForRead() {
  ensurePrivateBuffer();
  return private_buffer_->reserveForRead();
}

ReservationSingleSlice CopyOnWriteBuffer::reserveSingleSlice(uint64_t length, bool separate_slice) {
  ensurePrivateBuffer();
  return private_buffer_->reserveSingleSlice(length, separate_slice);
}

size_t CopyOnWriteBuffer::addFragments(absl::Span<const absl::string_view> fragments) {
  ensurePrivateBuffer();
  return private_buffer_->addFragments(fragments);
}

void CopyOnWriteBuffer::commit(uint64_t length, absl::Span<RawSlice> slices,
                               ReservationSlicesOwnerPtr slices_owner) {
  // commit() is called by Reservation destructor, so we need to handle it properly.
  // For copy-on-write buffers, we create a private buffer and manually add the committed data.
  UNREFERENCED_PARAMETER(slices_owner);
  ensurePrivateBuffer();

  // Add the data from the slices to the private buffer.
  for (size_t i = 0; i < slices.size() && length > 0; ++i) {
    const auto& slice = slices[i];
    const uint64_t to_add = std::min(length, static_cast<uint64_t>(slice.len_));
    if (to_add > 0) {
      private_buffer_->add(slice.mem_, to_add);
      length -= to_add;
    }
  }
}

// Factory functions.
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
