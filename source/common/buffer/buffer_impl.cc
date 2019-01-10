#include "common/buffer/buffer_impl.h"

#include <cstdint>
#include <string>

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/stack_array.h"

#include "event2/buffer.h"

namespace Envoy {
namespace Buffer {

/**
 * Copy a Reservation to a RawSlice. The public interface of RawSlice makes specific
 * fields immutable (the comments in the declaration of RawSlice explain why), so this
 * is a convenience function that encapsulates the required casting.
 * @param lhs the slice to be overwritten with a copy of rhs.
 * @param rhs the reservation to copy.
 */
static inline void Copy(RawSlice& lhs, const Slice::Reservation& rhs) {
  *(const_cast<void**>(&(lhs.mem_))) = rhs.mem_;
  lhs.len_ = rhs.len_;
}

void OwnedImpl::add(const void* data, uint64_t size) {
  const char* src = static_cast<const char*>(data);
  bool new_slice_needed = slices_.empty();
  while (size != 0) {
    if (new_slice_needed) {
      slices_.emplace_back(OwnedSlice::create(size));
    }
    uint64_t copy_size = slices_.back()->append(src, size);
    src += copy_size;
    size -= copy_size;
    length_ += copy_size;
    new_slice_needed = true;
  }
}

void OwnedImpl::addBufferFragment(BufferFragment& fragment) {
  length_ += fragment.size();
  slices_.emplace_back(std::make_unique<UnownedSlice>(fragment));
}

void OwnedImpl::add(absl::string_view data) { add(data.data(), data.size()); }

void OwnedImpl::add(const Instance& data) {
  uint64_t num_slices = data.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, RawSlice, num_slices);
  data.getRawSlices(slices.begin(), num_slices);
  for (const RawSlice& slice : slices) {
    add(slice.mem_, slice.len_);
  }
}

void OwnedImpl::prepend(absl::string_view data) {
  slices_.emplace_front(OwnedSlice::create(data.data(), data.size()));
  length_ += data.size();
}

void OwnedImpl::prepend(Instance& data) {
  OwnedImpl& other = static_cast<OwnedImpl&>(data);
  while (!other.slices_.empty()) {
    uint64_t slice_size = other.slices_.back()->dataSize();
    length_ += slice_size;
    slices_.emplace_front(std::move(other.slices_.back()));
    other.slices_.pop_back();
    other.length_ -= slice_size;
  }
  other.postProcess();
}

void OwnedImpl::commit(RawSlice* iovecs, uint64_t num_iovecs) {
  if (num_iovecs == 0) {
    return;
  }
  // Find the slices in the buffer that correspond to the iovecs:
  // First, scan backward from the end of the buffer to find the last slice containing
  // any content. Reservations are made from the end of the buffer, and out-of-order commits
  // aren't supported, so any slices before this point cannot match the iovecs being committed.
  ssize_t slice_index = static_cast<ssize_t>(slices_.size()) - 1;
  while (slice_index >= 0 && slices_[slice_index]->dataSize() == 0) {
    slice_index--;
  }
  if (slice_index < 0) {
    // There was no slice containing any data, so rewind the iterator at the first slice.
    slice_index = 0;
  }

  // Next, scan forward and attempt to match the slices against iovecs.
  uint64_t num_slices_committed = 0;
  while (num_slices_committed < num_iovecs) {
    if (slices_[slice_index]->commit(iovecs[num_slices_committed])) {
      length_ += iovecs[num_slices_committed].len_;
      num_slices_committed++;
    }
    slice_index++;
    if (slice_index == static_cast<ssize_t>(slices_.size())) {
      break;
    }
  }

  ASSERT(num_slices_committed > 0);
}

void OwnedImpl::copyOut(size_t start, uint64_t size, void* data) const {
  uint8_t* dest = static_cast<uint8_t*>(data);
  for (size_t slice_index = 0; slice_index < slices_.size(); slice_index++) {
    const auto& slice = slices_[slice_index];
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
    uint64_t slice_size = slices_.front()->dataSize();
    if (slice_size <= size) {
      slices_.pop_front();
      length_ -= slice_size;
      size -= slice_size;
    } else {
      slices_.front()->drain(size);
      length_ -= size;
      size = 0;
    }
  }
}

uint64_t OwnedImpl::getRawSlices(RawSlice* out, uint64_t out_size) const {
  uint64_t i = 0;
  for (size_t slice_index = 0; slice_index < slices_.size(); slice_index++) {
    const auto& slice = slices_[slice_index];
    if (slice->dataSize() == 0) {
      continue;
    }
    if (i < out_size) {
      // The cumbersome cast here allows RawSlice::mem_ to remain immutable by everything
      // outside OwnedImpl. The comments accompanying the RawSlice declaration in
      // include/envoy/buffer/buffer.h provide more context on why the immutability
      // is important.
      *(const_cast<void**>(&(out[i].mem_))) = slice->data();
      out[i].len_ = slice->dataSize();
    }
    i++;
  }
  return i;
}

uint64_t OwnedImpl::length() const {
  // When running in debug mode, verify that the precomputed length matches the sum
  // of the lengths of the slices.
  ASSERT(length_ == [this]() -> uint64_t {
    uint64_t length = 0;
    for (size_t slice_index = 0; slice_index < slices_.size(); slice_index++) {
      length += slices_[slice_index]->dataSize();
    }
    return length;
  }());

  return length_;
}

void* OwnedImpl::linearize(uint32_t size) {
  if (slices_.empty()) {
    return nullptr;
  }
  uint64_t linearized_size = 0;
  uint64_t num_slices_to_linearize = 0;
  for (size_t slice_index = 0; slice_index < slices_.size(); slice_index++) {
    const auto& slice = slices_[slice_index];
    num_slices_to_linearize++;
    linearized_size += slice->dataSize();
    if (linearized_size >= size) {
      break;
    }
  }
  if (num_slices_to_linearize > 1) {
    auto new_slice = OwnedSlice::create(linearized_size);
    uint64_t bytes_copied = 0;
    Slice::Reservation reservation = new_slice->reserve(linearized_size);
    ASSERT(reservation.mem_ != nullptr);
    ASSERT(reservation.len_ == linearized_size);
    auto dest = static_cast<uint8_t*>(reservation.mem_);
    do {
      uint64_t data_size = slices_.front()->dataSize();
      memcpy(dest, slices_.front()->data(), data_size);
      bytes_copied += data_size;
      dest += data_size;
      slices_.pop_front();
    } while (bytes_copied < linearized_size);
    ASSERT(dest == static_cast<const uint8_t*>(reservation.mem_) + linearized_size);
    new_slice->commit(reservation);
    slices_.emplace_front(std::move(new_slice));
  }
  return slices_.front()->data();
}

void OwnedImpl::move(Instance& rhs) {
  // We do the static cast here because in practice we only have one buffer implementation right
  // now and this is safe. This is a reasonable compromise in a high performance path where we want
  // to maintain an abstraction.
  ASSERT(dynamic_cast<OwnedImpl*>(&rhs) != nullptr);
  OwnedImpl& other = static_cast<OwnedImpl&>(rhs);
  while (!other.slices_.empty()) {
    const uint64_t slice_size = other.slices_.front()->dataSize();
    slices_.emplace_back(std::move(other.slices_.front()));
    other.slices_.pop_front();
    length_ += slice_size;
    other.length_ -= slice_size;
  }
  other.postProcess();
}

void OwnedImpl::move(Instance& rhs, uint64_t length) {
  // See move() above for why we do the static cast.
  ASSERT(dynamic_cast<OwnedImpl*>(&rhs) != nullptr);
  OwnedImpl& other = static_cast<OwnedImpl&>(rhs);
  while (length != 0 && !other.slices_.empty()) {
    const uint64_t slice_size = other.slices_.front()->dataSize();
    const uint64_t copy_size = std::min(slice_size, length);
    if (copy_size == 0) {
      other.slices_.pop_front();
    } else if (copy_size < slice_size) {
      // TODO(brian-pane) add reference-counting to allow slices to share their storage
      // and eliminate the copy for this partial-slice case?
      add(other.slices_.front()->data(), copy_size);
      other.slices_.front()->drain(copy_size);
      other.length_ -= copy_size;
    } else {
      slices_.emplace_back(std::move(other.slices_.front()));
      other.slices_.pop_front();
      length_ += slice_size;
      other.length_ -= slice_size;
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
    for (uint64_t i = 0; i < num_slices; i++) {
      slices[i].len_ = 0;
    }
    commit(slices, num_slices);
    return {static_cast<int>(result.rc_), result.errno_};
  }
  uint64_t bytes_to_commit = result.rc_;
  ASSERT(bytes_to_commit <= max_length);
  for (uint64_t i = 0; i < num_slices; i++) {
    slices[i].len_ = std::min(slices[i].len_, static_cast<size_t>(bytes_to_commit));
    bytes_to_commit -= slices[i].len_;
  }
  commit(slices, num_slices);
  return {static_cast<int>(result.rc_), result.errno_};
}

uint64_t OwnedImpl::reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) {
  if (num_iovecs == 0) {
    return 0;
  }
  uint64_t reservable_size = slices_.empty() ? 0 : slices_.back()->reservableSize();
  if (reservable_size == 0) {
    // Special case: there is no usable space in the last slice, so allocate a
    // new slice big enough to hold the entire reservation.
    slices_.emplace_back(OwnedSlice::create(length));
    Copy(iovecs[0], slices_.back()->reserve(length));
    ASSERT(iovecs[0].len_ == length);
    ASSERT(iovecs[0].mem_ != nullptr);
    return 1;
  } else if (reservable_size >= length) {
    // Special case: there is at least enough space in the last slice to hold
    // the reqested reservation.
    Copy(iovecs[0], slices_.back()->reserve(length));
    ASSERT(iovecs[0].len_ == length);
    ASSERT(iovecs[0].mem_ != nullptr);
    return 1;
  } else if (num_iovecs == 1) {
    // Special case: there is some space available in the last slice, but not
    // enough to hold the entire reservation. And the caller has only provided one
    // iovec,so the reservation cannot be split among multiple slices. Create a new
    // slice big enough to hold the entire reservation (and waste the leftover space
    // in what used to be the last slice).
    slices_.emplace_back(OwnedSlice::create(length));
    Copy(iovecs[0], slices_.back()->reserve(length));
    ASSERT(iovecs[0].len_ == length);
    ASSERT(iovecs[0].mem_ != nullptr);
    return 1;
  } else {
    // General case: the last slice has enough space to fulfill part but not all of the
    // requested reservation, so split the reservation between that slice and a newly
    // allocated slice.
    Copy(iovecs[0], slices_.back()->reserve(reservable_size));
    uint64_t remaining_size = length - reservable_size;
    slices_.emplace_back(OwnedSlice::create(remaining_size));
    Copy(iovecs[1], slices_.back()->reserve(remaining_size));
    ASSERT(iovecs[0].len_ + iovecs[1].len_ == length);
    ASSERT(iovecs[0].mem_ != nullptr);
    ASSERT(iovecs[1].mem_ != nullptr);
    return 2;
  }
}

ssize_t OwnedImpl::search(const void* data, uint64_t size, size_t start) const {
  // This implementation uses the same search algorithm as evbuffer_search(), a naive
  // scan that requires O(M*N) comparisons in the worst case.
  // TODO(brian-pane): replace this with a more efficient search if it shows up
  // prominently in CPU profiling.
  if (size == 0) {
    return 0;
  }
  ssize_t offset = 0;
  const uint8_t* needle = static_cast<const uint8_t*>(data);
  for (size_t slice_index = 0; slice_index < slices_.size(); slice_index++) {
    const auto& slice = slices_[slice_index];
    uint64_t slice_size = slice->dataSize();
    if (slice_size <= start) {
      start -= slice_size;
      offset += slice_size;
      continue;
    }
    const uint8_t* slice_start = static_cast<const uint8_t*>(slice->data());
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
      size_t match_index = slice_index;
      const uint8_t* match_next = first_byte_match + 1;
      const uint8_t* match_end = haystack_end;
      for (; i < size; i++) {
        if (match_next >= match_end) {
          // We've hit the end of this slice, so continue checking against the next slice.
          match_index++;
          if (match_index == slices_.size()) {
            // We've hit the end of the entire buffer.
            break;
          }
          const auto& match_slice = slices_[match_index];
          match_next = static_cast<const uint8_t*>(match_slice->data());
          match_end = match_next + match_slice->dataSize();
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

OwnedImpl::OwnedImpl() = default;

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
