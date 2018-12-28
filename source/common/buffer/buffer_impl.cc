#include "common/buffer/buffer_impl.h"

#include <cstdint>
#include <iostream>
#include <string>

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/stack_array.h"

#include "event2/buffer.h"

namespace Envoy {
namespace Buffer {

static uint64_t SliceSize(uint64_t data_size) {
  uint64_t slice_size = 32;
  while (slice_size < data_size) {
    slice_size <<= 1;
    if (slice_size == 0) {
      // Integer overflow
      return data_size;
    }
  }
  return slice_size;
}

class OwnedBufferSlice : public BufferSlice {
public:
  OwnedBufferSlice(uint64_t size) : reserved_(0), reservable_(0), size_(SliceSize(size)) {
    base_.reset(new uint8_t[size_]);
  }

  OwnedBufferSlice(const void* data, uint64_t size) : OwnedBufferSlice(size) {
    memcpy(&(base_[0]), data, size);
    reserved_ = reservable_ = size;
  }

  // BufferSlice
  const void* data() const override { return &(base_[data_]); }

  void* data() override { return &(base_[data_]); }

  uint64_t dataSize() const override { return reserved_ - data_; }

  void drain(uint64_t size) override {
    ASSERT(data_ + size <= reserved_);
    data_ += size;
    if (data_ == reservable_) {
      // There is no more content in the slice, and there are no outstanding reservations,
      // so reset the Data section to the start of the slice to facilitate reuse.
      data_ = reserved_ = reservable_ = 0;
    }
  }

  uint64_t reservableSize() const override { return size_ - reservable_; }

  Reservation reserve(uint64_t size) override {
    uint64_t available_size = size_ - reservable_;
    if (available_size == 0) {
      return {nullptr, 0};
    }
    uint64_t reservation_size = std::min(size, available_size);
    void* reservation = &(base_[reservable_]);
    reservable_ += reservation_size;
    num_reservations_++;
    return {reservation, reservation_size};
  }

  bool commit(const Reservation& reservation, uint64_t size) override {
    if (static_cast<const uint8_t*>(reservation.first) !=
            static_cast<const uint8_t*>(&(base_[0])) + reserved_ ||
        reserved_ + size > reservable_) {
      // The reservation is not from this OwnedBufferSlice.
      return false;
    }
    ASSERT(size <= reservation.second);
    ASSERT(num_reservations_ > 0);
    reserved_ += size;
    num_reservations_--;
    if (num_reservations_ == 0) {
      reservable_ = reserved_;
    }
    return true;
  }

private:
  std::unique_ptr<uint8_t[]> base_;

  /** Offset in bytes from the start of the slice to the start of the Data section */
  uint64_t data_{0};

  /** Offset in bytes from the start of the slice to the start of the Reserved section */
  uint64_t reserved_;

  /** Offset in bytes from the start of the slice to the start of the Reservable section */
  uint64_t reservable_;

  /** Total number of bytes in the slice */
  uint64_t size_;

  /** Number of outstanding reservations that haven't yet been committed. */
  unsigned num_reservations_{0};
};

class UnownedBufferSlice : public BufferSlice {
public:
  UnownedBufferSlice(BufferFragment& fragment) : fragment_(fragment), data_(0) {}
  ~UnownedBufferSlice() override { fragment_.done(); }

  // BufferSlice
  const void* data() const override {
    return static_cast<const uint8_t*>(fragment_.data()) + data_;
  }

  void* data() override {
    return static_cast<uint8_t*>(const_cast<void*>(fragment_.data())) + data_;
  }

  uint64_t dataSize() const override { return fragment_.size() - data_; }

  void drain(uint64_t size) override {
    ASSERT(data_ + size <= fragment_.size());
    data_ += size;
  }

  uint64_t reservableSize() const override { return 0; }

  Reservation reserve(uint64_t) override { return {nullptr, 0}; }

  bool commit(const Reservation&, uint64_t) override { return false; }

private:
  BufferFragment& fragment_;

  /**
   * Offset in bytes from the start of the fragment to the start of the Data section
   * (increases with each call to drain()).
   */
  uint64_t data_;
};

void OwnedImpl::append(const void* data, uint64_t size) {
  const char* src = static_cast<const char*>(data);
  while (size != 0) {
    if (slices_.empty() || slices_.back()->reservableSize() == 0) {
      slices_.emplace_back(std::make_unique<OwnedBufferSlice>(size));
    }
    BufferSlice::Reservation reservation = slices_.back()->reserve(size);
    ASSERT(reservation.first != nullptr);
    ASSERT(reservation.second != 0);
    memcpy(reservation.first, src, reservation.second);
    slices_.back()->commit(reservation, reservation.second);
    src += reservation.second;
    size -= reservation.second;
  }
}

void OwnedImpl::add(const void* data, uint64_t size) { append(data, size); }

void OwnedImpl::addBufferFragment(BufferFragment& fragment) {
  slices_.emplace_back(std::make_unique<UnownedBufferSlice>(fragment));
}

void OwnedImpl::add(absl::string_view data) { append(data.data(), data.size()); }

void OwnedImpl::add(const Instance& data) {
  uint64_t num_slices = data.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, RawSlice, num_slices);
  data.getRawSlices(slices.begin(), num_slices);
  for (const RawSlice& slice : slices) {
    append(slice.mem_, slice.len_);
  }
}

void OwnedImpl::prepend(absl::string_view data) {
  slices_.emplace_front(std::make_unique<OwnedBufferSlice>(data.data(), data.size()));
}

void OwnedImpl::prepend(Instance& data) {
  OwnedImpl& other = static_cast<OwnedImpl&>(data);
  while (!other.slices_.empty()) {
    slices_.emplace_front(std::move(other.slices_.back()));
    other.slices_.pop_back();
  }
  other.postProcess();
}

void OwnedImpl::commit(RawSlice* iovecs, uint64_t num_iovecs) {
  int64_t i = num_iovecs;
  i--;
  for (auto iter = slices_.rbegin(); i >= 0 && iter != slices_.rend(); ++iter) {
    BufferSlice::Reservation reservation{iovecs[i].mem_, iovecs[i].len_};
    if ((*iter)->commit(reservation, iovecs[i].len_)) {
      i--;
    }
  }
  ASSERT(i < 0);
}

void OwnedImpl::copyOut(size_t start, uint64_t size, void* data) const {
  uint8_t* dest = static_cast<uint8_t*>(data);
  for (const auto& slice : slices_) {
    if (size == 0) {
      break;
    }
    uint64_t data_size = slice->dataSize();
    if (data_size <= start) {
      start -= data_size;
      continue;
    }
    uint64_t copy_size = std::min(size, data_size - start);
    memcpy(dest, static_cast<const uint8_t*>(slice->data()) + start, copy_size);
    size -= copy_size;
    dest += copy_size;
    start = 0;
  }
  ASSERT(size == 0);
}

void OwnedImpl::drain(uint64_t size) {
  while (size != 0) {
    if (slices_.empty()) {
      break;
    }
    if (slices_.front()->dataSize() <= size) {
      size -= slices_.front()->dataSize();
      slices_.pop_front();
    } else {
      slices_.front()->drain(size);
      size = 0;
    }
  }
}

uint64_t OwnedImpl::getRawSlices(RawSlice* out, uint64_t out_size) const {
  uint64_t i = 0;
  for (const auto& slice : slices_) {
    if (slice->dataSize() == 0) {
      continue;
    }
    if (i < out_size) {
      out[i].mem_ = slice->data();
      out[i].len_ = slice->dataSize();
    }
    i++;
  }
  return i;
}

uint64_t OwnedImpl::length() const {
  uint64_t length = 0;
  for (const auto& slice : slices_) {
    length += slice->dataSize();
  }
  return length;
}

void* OwnedImpl::linearize(uint32_t size) {
  if (slices_.empty()) {
    return nullptr;
  }
  uint64_t linearized_size = 0;
  uint64_t num_slices_to_linearize = 0;
  for (const auto& slice : slices_) {
    num_slices_to_linearize++;
    linearized_size += slice->dataSize();
    if (linearized_size >= size) {
      break;
    }
  }
  if (num_slices_to_linearize > 1) {
    auto new_slice = std::make_unique<OwnedBufferSlice>(linearized_size);
    uint64_t bytes_copied = 0;
    BufferSlice::Reservation reservation = new_slice->reserve(linearized_size);
    ASSERT(reservation.first != nullptr);
    ASSERT(reservation.second == linearized_size);
    auto dest = static_cast<uint8_t*>(reservation.first);
    do {
      uint64_t data_size = slices_.front()->dataSize();
      memcpy(dest, slices_.front()->data(), data_size);
      bytes_copied += data_size;
      dest += data_size;
      slices_.pop_front();
    } while (bytes_copied < linearized_size);
    ASSERT(dest == static_cast<const uint8_t*>(reservation.first) + linearized_size);
    new_slice->commit(reservation, linearized_size);
    slices_.emplace_front(std::move(new_slice));
  }
  return slices_.front()->data();
}

void OwnedImpl::move(Instance& rhs) {
  // We do the static cast here because in practice we only have one buffer implementation right
  // now and this is safe. This is a reasonable compromise in a high performance path where we want
  // to maintain an abstraction.
  OwnedImpl& other = static_cast<OwnedImpl&>(rhs);
  while (!other.slices_.empty()) {
    slices_.emplace_back(std::move(other.slices_.front()));
    other.slices_.pop_front();
  }
  other.postProcess();
}

void OwnedImpl::move(Instance& rhs, uint64_t length) {
  // See move() above for why we do the static cast.
  OwnedImpl& other = static_cast<OwnedImpl&>(rhs);
  while (!other.slices_.empty()) {
    if (length == 0) {
      break;
    }
    uint64_t copy_size = std::min(other.slices_.front()->dataSize(), length);
    if (copy_size == 0) {
      other.slices_.pop_front();
    } else if (copy_size < other.slices_.front()->dataSize()) {
      // TODO add reference-counting to allow slices to share their storage
      // and eliminate the copy for this partial-slice case?
      append(other.slices_.front()->data(), copy_size);
      other.slices_.front()->drain(copy_size);
    } else {
      slices_.emplace_back(std::move(other.slices_.front()));
      other.slices_.pop_front();
    }
    length -= copy_size;
  }
  other.postProcess();
}

Api::SysCallIntResult OwnedImpl::read(int fd, uint64_t max_length) {
  if (max_length == 0) {
    return {0, 0};
  }
  constexpr uint64_t MaxSlices = 2;
  RawSlice slices[MaxSlices];
  const uint64_t num_slices = reserve(max_length, slices, MaxSlices);
  STACK_ARRAY(iov, iovec, num_slices);
  uint64_t num_slices_to_read = 0;
  uint64_t num_bytes_to_read = 0;
  for (; num_slices_to_read < num_slices && num_bytes_to_read < max_length; num_slices_to_read++) {
    iov[num_slices_to_read].iov_base = slices[num_slices_to_read].mem_;
    const size_t slice_length = std::min(slices[num_slices_to_read].len_,
                                         static_cast<size_t>(max_length - num_bytes_to_read));
    iov[num_slices_to_read].iov_len = slice_length;
    num_bytes_to_read += slice_length;
  }
  ASSERT(num_slices_to_read <= MaxSlices);
  ASSERT(num_bytes_to_read <= max_length);
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSizeResult result =
      os_syscalls.readv(fd, iov.begin(), static_cast<int>(num_slices_to_read));
  if (result.rc_ < 0) {
    return {static_cast<int>(result.rc_), result.errno_};
  }
  uint64_t num_slices_to_commit = 0;
  uint64_t bytes_to_commit = result.rc_;
  ASSERT(bytes_to_commit <= max_length);
  while (bytes_to_commit != 0) {
    slices[num_slices_to_commit].len_ =
        std::min(slices[num_slices_to_commit].len_, static_cast<size_t>(bytes_to_commit));
    ASSERT(bytes_to_commit >= slices[num_slices_to_commit].len_);
    bytes_to_commit -= slices[num_slices_to_commit].len_;
    num_slices_to_commit++;
  }
  ASSERT(num_slices_to_commit <= num_slices);
  commit(slices, num_slices_to_commit);
  return {static_cast<int>(result.rc_), result.errno_};
}

uint64_t OwnedImpl::reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) {
  // TODO support the use of space in more than one slice.
  if (num_iovecs == 0) {
    return 0;
  }
  if (slices_.empty() || slices_.back()->reservableSize() < length) {
    slices_.emplace_back(std::make_unique<OwnedBufferSlice>(length));
  }
  ASSERT(slices_.back()->reservableSize() >= length);
  BufferSlice::Reservation reservation = slices_.back()->reserve(length);
  iovecs[0].mem_ = reservation.first;
  iovecs[0].len_ = reservation.second;
  return 1;
}

ssize_t OwnedImpl::search(const void* data, uint64_t size, size_t start) const {
  // This implementation uses the same search algorithm as evbuffer_search(), a naive
  // scan that requires O(M*N) comparisons in the worst case.
  // TODO: replace this with a more efficient search if it shows up prominently in CPU profiling.
  if (size == 0) {
    return 0;
  }
  ssize_t offset = 0;
  const uint8_t* needle = static_cast<const uint8_t*>(data);
  for (auto iter = slices_.begin(); iter != slices_.end(); ++iter) {
    uint64_t slice_size = (*iter)->dataSize();
    if (slice_size <= start) {
      start -= slice_size;
      offset += slice_size;
      continue;
    }
    const uint8_t* slice_start = static_cast<const uint8_t*>((*iter)->data());
    const uint8_t* haystack = slice_start;
    const uint8_t* haystack_end = haystack + slice_size;
    haystack += start;
    while (haystack < haystack_end) {
      // Search within this slice for the first byte of the needle.
      const uint8_t* first_byte_match =
          static_cast<const uint8_t*>(memchr(haystack, needle[0], haystack_end - haystack));
      if (first_byte_match == nullptr) {
        break;
      }
      // After finding a match for the first byte of the needle, check whether the following
      // bytes in the buffer match the remainder of the needle. Note that the match can span
      // two or more slices.
      size_t i = 1;
      auto match_iter = iter;
      const uint8_t* match_next = first_byte_match + 1;
      const uint8_t* match_end = haystack_end;
      for (; i < size; i++) {
        if (match_next >= match_end) {
          // We've hit the end of this slice, so continue checking against the next slice.
          ++match_iter;
          if (match_iter == slices_.end()) {
            // We've hit the end of the entire buffer.
            break;
          }
          match_next = static_cast<const uint8_t*>((*match_iter)->data());
          match_end = match_next + (*match_iter)->dataSize();
        }
        if (*match_next++ != needle[i]) {
          break;
        }
      }
      if (i == size) {
        // Successful match of the entire needle.
        return offset + (first_byte_match - slice_start);
      }
      // If this wasn't a successful match, start scanning again at the next byte.
      haystack = first_byte_match + 1;
    }
    start = 0;
    offset += slice_size;
  }
  return -1;
}

Api::SysCallIntResult OwnedImpl::write(int fd) {
  constexpr uint64_t MaxSlices = 16;
  RawSlice slices[MaxSlices];
  const uint64_t num_slices = std::min(getRawSlices(slices, MaxSlices), MaxSlices);
  STACK_ARRAY(iov, iovec, num_slices);
  uint64_t num_slices_to_write = 0;
  for (uint64_t i = 0; i < num_slices; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      iov[num_slices_to_write].iov_base = slices[i].mem_;
      iov[num_slices_to_write].iov_len = slices[i].len_;
      num_slices_to_write++;
    }
  }
  if (num_slices_to_write == 0) {
    return {0, 0};
  }
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSizeResult result = os_syscalls.writev(fd, iov.begin(), num_slices_to_write);
  if (result.rc_ > 0) {
    drain(static_cast<uint64_t>(result.rc_));
  }
  return {static_cast<int>(result.rc_), result.errno_};
}

OwnedImpl::OwnedImpl() {}

OwnedImpl::OwnedImpl(absl::string_view data) : OwnedImpl() { add(data); }

OwnedImpl::OwnedImpl(const Instance& data) : OwnedImpl() { add(data); }

OwnedImpl::OwnedImpl(const void* data, uint64_t size) : OwnedImpl() { add(data, size); }

std::string OwnedImpl::toString() const {
  uint64_t num_slices = getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, RawSlice, num_slices);
  getRawSlices(slices.begin(), num_slices);
  size_t len = 0;
  for (const RawSlice& slice : slices) {
    len += slice.len_;
  }
  std::string output;
  output.reserve(len);
  for (const RawSlice& slice : slices) {
    output.append(static_cast<const char*>(slice.mem_), slice.len_);
  }

  return output;
}

void OwnedImpl::postProcess() {}

} // namespace Buffer
} // namespace Envoy
