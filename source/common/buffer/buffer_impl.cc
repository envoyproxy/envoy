#include "common/buffer/buffer_impl.h"

#include <cstdint>
#include <string>

#include "common/common/assert.h"
#include "common/common/stack_array.h"

#include "event2/buffer.h"

namespace Envoy {
namespace Buffer {

void OwnedImpl::add(const void* data, uint64_t size) {
  if (old_impl_) {
    evbuffer_add(buffer_.get(), data, size);
  } else {
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
}

void OwnedImpl::addBufferFragment(BufferFragment& fragment) {
  if (old_impl_) {
    evbuffer_add_reference(
        buffer_.get(), fragment.data(), fragment.size(),
        [](const void*, size_t, void* arg) { static_cast<BufferFragment*>(arg)->done(); },
        &fragment);
  } else {
    length_ += fragment.size();
    slices_.emplace_back(std::make_unique<UnownedSlice>(fragment));
  }
}

void OwnedImpl::add(absl::string_view data) {
  if (old_impl_) {
    evbuffer_add(buffer_.get(), data.data(), data.size());
  } else {
    add(data.data(), data.size());
  }
}

void OwnedImpl::add(const Instance& data) {
  ASSERT(&data != this);
  uint64_t num_slices = data.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, RawSlice, num_slices);
  data.getRawSlices(slices.begin(), num_slices);
  for (const RawSlice& slice : slices) {
    add(slice.mem_, slice.len_);
  }
}

void OwnedImpl::prepend(absl::string_view data) {
  if (old_impl_) {
    // Prepending an empty string seems to mess up libevent internally.
    // evbuffer_prepend doesn't have a check for empty (unlike
    // evbuffer_prepend_buffer which does). This then results in an allocation of
    // an empty chain, which causes problems with a following move/append. This
    // only seems to happen the original buffer was created via
    // addBufferFragment(), this forces the code execution path in
    // evbuffer_prepend related to immutable buffers.
    if (data.empty()) {
      return;
    }
    evbuffer_prepend(buffer_.get(), data.data(), data.size());
  } else {
    uint64_t size = data.size();
    bool new_slice_needed = slices_.empty();
    while (size != 0) {
      if (new_slice_needed) {
        slices_.emplace_front(OwnedSlice::create(size));
      }
      uint64_t copy_size = slices_.front()->prepend(data.data(), size);
      size -= copy_size;
      length_ += copy_size;
      new_slice_needed = true;
    }
  }
}

void OwnedImpl::prepend(Instance& data) {
  ASSERT(&data != this);
  ASSERT(isSameBufferImpl(data));
  // See the comments in move() for why we do the static_cast.
  if (old_impl_) {
    ASSERT(dynamic_cast<LibEventInstance*>(&data) != nullptr);
    int rc =
        evbuffer_prepend_buffer(buffer_.get(), static_cast<LibEventInstance&>(data).buffer().get());
    ASSERT(rc == 0);
    ASSERT(data.length() == 0);
    static_cast<LibEventInstance&>(data).postProcess();
  } else {
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
}

void OwnedImpl::commit(RawSlice* iovecs, uint64_t num_iovecs) {
  if (old_impl_) {
    int rc =
        evbuffer_commit_space(buffer_.get(), reinterpret_cast<evbuffer_iovec*>(iovecs), num_iovecs);
    ASSERT(rc == 0);
  } else {
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
      if (!slices_[0]) {
        return;
      }
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
}

void OwnedImpl::copyOut(size_t start, uint64_t size, void* data) const {
  if (old_impl_) {
    ASSERT(start + size <= length());

    evbuffer_ptr start_ptr;
    int rc = evbuffer_ptr_set(buffer_.get(), &start_ptr, start, EVBUFFER_PTR_SET);
    ASSERT(rc != -1);

    ev_ssize_t copied = evbuffer_copyout_from(buffer_.get(), &start_ptr, data, size);
    ASSERT(static_cast<uint64_t>(copied) == size);
  } else {
    uint64_t bytes_to_skip = start;
    uint8_t* dest = static_cast<uint8_t*>(data);
    for (const auto& slice : slices_) {
      if (size == 0) {
        break;
      }
      uint64_t data_size = slice->dataSize();
      if (data_size <= bytes_to_skip) {
        // The offset where the caller wants to start copying is after the end of this slice,
        // so just skip over this slice completely.
        bytes_to_skip -= data_size;
        continue;
      }
      uint64_t copy_size = std::min(size, data_size - bytes_to_skip);
      memcpy(dest, slice->data() + bytes_to_skip, copy_size);
      size -= copy_size;
      dest += copy_size;
      // Now that we've started copying, there are no bytes left to skip over. If there
      // is any more data to be copied, the next iteration can start copying from the very
      // beginning of the next slice.
      bytes_to_skip = 0;
    }
    ASSERT(size == 0);
  }
}

void OwnedImpl::drain(uint64_t size) {
  if (old_impl_) {
    ASSERT(size <= length());
    int rc = evbuffer_drain(buffer_.get(), size);
    ASSERT(rc == 0);
  } else {
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
}

uint64_t OwnedImpl::getRawSlices(RawSlice* out, uint64_t out_size) const {
  if (old_impl_) {
    return evbuffer_peek(buffer_.get(), -1, nullptr, reinterpret_cast<evbuffer_iovec*>(out),
                         out_size);
  } else {
    uint64_t num_slices = 0;
    for (const auto& slice : slices_) {
      if (slice->dataSize() == 0) {
        continue;
      }
      if (num_slices < out_size) {
        out[num_slices].mem_ = slice->data();
        out[num_slices].len_ = slice->dataSize();
      }
      // Per the definition of getRawSlices in include/envoy/buffer/buffer.h, we need to return
      // the total number of slices needed to access all the data in the buffer, which can be
      // larger than out_size. So we keep iterating and counting non-empty slices here, even
      // if all the caller-supplied slices have been filled.
      num_slices++;
    }
    return num_slices;
  }
}

uint64_t OwnedImpl::length() const {
  if (old_impl_) {
    return evbuffer_get_length(buffer_.get());
  } else {
#ifndef NDEBUG
    // When running in debug mode, verify that the precomputed length matches the sum
    // of the lengths of the slices.
    uint64_t length = 0;
    for (const auto& slice : slices_) {
      length += slice->dataSize();
    }
    ASSERT(length == length_);
#endif

    return length_;
  }
}

void* OwnedImpl::linearize(uint32_t size) {
  RELEASE_ASSERT(size <= length(), "Linearize size exceeds buffer size");
  if (old_impl_) {
    void* const ret = evbuffer_pullup(buffer_.get(), size);
    RELEASE_ASSERT(ret != nullptr || size == 0,
                   "Failure to linearize may result in buffer overflow by the caller.");
    return ret;
  } else {
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
}

void OwnedImpl::move(Instance& rhs) {
  ASSERT(&rhs != this);
  ASSERT(isSameBufferImpl(rhs));
  if (old_impl_) {
    // We do the static cast here because in practice we only have one buffer implementation right
    // now and this is safe. Using the evbuffer move routines require having access to both
    // evbuffers. This is a reasonable compromise in a high performance path where we want to
    // maintain an abstraction in case we get rid of evbuffer later.
    ASSERT(dynamic_cast<LibEventInstance*>(&rhs) != nullptr);
    int rc = evbuffer_add_buffer(buffer_.get(), static_cast<LibEventInstance&>(rhs).buffer().get());
    ASSERT(rc == 0);
    static_cast<LibEventInstance&>(rhs).postProcess();
  } else {
    // We do the static cast here because in practice we only have one buffer implementation right
    // now and this is safe. This is a reasonable compromise in a high performance path where we
    // want to maintain an abstraction.
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
}

void OwnedImpl::move(Instance& rhs, uint64_t length) {
  ASSERT(&rhs != this);
  ASSERT(isSameBufferImpl(rhs));
  if (old_impl_) {
    // See move() above for why we do the static cast.
    int rc = evbuffer_remove_buffer(static_cast<LibEventInstance&>(rhs).buffer().get(),
                                    buffer_.get(), length);
    ASSERT(static_cast<uint64_t>(rc) == length);
    static_cast<LibEventInstance&>(rhs).postProcess();
  } else {
    // See move() above for why we do the static cast.
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
}

Api::IoCallUint64Result OwnedImpl::read(Network::IoHandle& io_handle, uint64_t max_length) {
  if (max_length == 0) {
    return Api::ioCallUint64ResultNoError();
  }
  constexpr uint64_t MaxSlices = 2;
  RawSlice slices[MaxSlices];
  const uint64_t num_slices = reserve(max_length, slices, MaxSlices);
  Api::IoCallUint64Result result = io_handle.readv(max_length, slices, num_slices);
  if (old_impl_) {
    if (!result.ok()) {
      return result;
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
  } else {
    uint64_t bytes_to_commit = result.ok() ? result.rc_ : 0;
    ASSERT(bytes_to_commit <= max_length);
    for (uint64_t i = 0; i < num_slices; i++) {
      slices[i].len_ = std::min(slices[i].len_, static_cast<size_t>(bytes_to_commit));
      bytes_to_commit -= slices[i].len_;
    }
    commit(slices, num_slices);
  }
  return result;
}

uint64_t OwnedImpl::reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) {
  if (num_iovecs == 0 || length == 0) {
    return 0;
  }
  if (old_impl_) {
    int ret = evbuffer_reserve_space(buffer_.get(), length,
                                     reinterpret_cast<evbuffer_iovec*>(iovecs), num_iovecs);
    RELEASE_ASSERT(ret >= 1, "Failure to allocate may result in callers writing to uninitialized "
                             "memory, buffer overflows, etc");
    return static_cast<uint64_t>(ret);
  } else {
    // Check whether there are any empty slices with reservable space at the end of the buffer.
    size_t first_reservable_slice = slices_.size();
    while (first_reservable_slice > 0) {
      if (slices_[first_reservable_slice - 1]->reservableSize() == 0) {
        break;
      }
      first_reservable_slice--;
      if (slices_[first_reservable_slice]->dataSize() != 0) {
        // There is some content in this slice, so anything in front of it is non-reservable.
        break;
      }
    }

    // Having found the sequence of reservable slices at the back of the buffer, reserve
    // as much space as possible from each one.
    uint64_t num_slices_used = 0;
    uint64_t bytes_remaining = length;
    size_t slice_index = first_reservable_slice;
    while (slice_index < slices_.size() && bytes_remaining != 0 && num_slices_used < num_iovecs) {
      auto& slice = slices_[slice_index];
      const uint64_t reservation_size = std::min(slice->reservableSize(), bytes_remaining);
      if (num_slices_used + 1 == num_iovecs && reservation_size < bytes_remaining) {
        // There is only one iovec left, and this next slice does not have enough space to
        // complete the reservation. Stop iterating, with last one iovec still unpopulated,
        // so the code following this loop can allocate a new slice to hold the rest of the
        // reservation.
        break;
      }
      iovecs[num_slices_used] = slice->reserve(reservation_size);
      bytes_remaining -= iovecs[num_slices_used].len_;
      num_slices_used++;
      slice_index++;
    }

    // If needed, allocate one more slice at the end to provide the remainder of the reservation.
    if (bytes_remaining != 0) {
      slices_.emplace_back(OwnedSlice::create(bytes_remaining));
      iovecs[num_slices_used] = slices_.back()->reserve(bytes_remaining);
      bytes_remaining -= iovecs[num_slices_used].len_;
      num_slices_used++;
    }

    ASSERT(num_slices_used <= num_iovecs);
    ASSERT(bytes_remaining == 0);
    return num_slices_used;
  }
}

ssize_t OwnedImpl::search(const void* data, uint64_t size, size_t start) const {
  if (old_impl_) {
    evbuffer_ptr start_ptr;
    if (-1 == evbuffer_ptr_set(buffer_.get(), &start_ptr, start, EVBUFFER_PTR_SET)) {
      return -1;
    }

    evbuffer_ptr result_ptr =
        evbuffer_search(buffer_.get(), static_cast<const char*>(data), size, &start_ptr);
    return result_ptr.pos;
  } else {
    // This implementation uses the same search algorithm as evbuffer_search(), a naive
    // scan that requires O(M*N) comparisons in the worst case.
    // TODO(brian-pane): replace this with a more efficient search if it shows up
    // prominently in CPU profiling.
    if (size == 0) {
      return (start <= length_) ? start : -1;
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
      const uint8_t* slice_start = slice->data();
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
        while (i < size) {
          if (match_next >= match_end) {
            // We've hit the end of this slice, so continue checking against the next slice.
            match_index++;
            if (match_index == slices_.size()) {
              // We've hit the end of the entire buffer.
              break;
            }
            const auto& match_slice = slices_[match_index];
            match_next = match_slice->data();
            match_end = match_next + match_slice->dataSize();
            continue;
          }
          if (*match_next++ != needle[i]) {
            break;
          }
          i++;
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
}

bool OwnedImpl::startsWith(absl::string_view data) const {
  if (length() < data.length()) {
    // Buffer is too short to contain data.
    return false;
  }

  if (data.length() == 0) {
    return true;
  }

  if (old_impl_) {
    evbuffer_ptr start_ptr, end_ptr;
    if (-1 == evbuffer_ptr_set(buffer_.get(), &start_ptr, 0, EVBUFFER_PTR_SET)) {
      return false;
    }

    if (-1 == evbuffer_ptr_set(buffer_.get(), &end_ptr, data.length(), EVBUFFER_PTR_SET)) {
      return false;
    }

    evbuffer_ptr result_ptr =
        evbuffer_search_range(buffer_.get(), data.data(), data.length(), &start_ptr, &end_ptr);
    return result_ptr.pos == 0;
  } else {
    const uint8_t* prefix = reinterpret_cast<const uint8_t*>(data.data());
    size_t size = data.length();
    for (const auto& slice : slices_) {
      uint64_t slice_size = slice->dataSize();
      const uint8_t* slice_start = slice->data();

      if (slice_size >= size) {
        // The remaining size bytes of data are in this slice.
        return memcmp(prefix, slice_start, size) == 0;
      }

      // Slice is smaller than data, see if the prefix matches.
      if (memcmp(prefix, slice_start, slice_size) != 0) {
        return false;
      }

      // Prefix matched. Continue looking at the next slice.
      prefix += slice_size;
      size -= slice_size;
    }

    // Less data in slices than length() reported.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

Api::IoCallUint64Result OwnedImpl::write(Network::IoHandle& io_handle) {
  constexpr uint64_t MaxSlices = 16;
  RawSlice slices[MaxSlices];
  const uint64_t num_slices = std::min(getRawSlices(slices, MaxSlices), MaxSlices);
  Api::IoCallUint64Result result = io_handle.writev(slices, num_slices);
  if (result.ok() && result.rc_ > 0) {
    drain(static_cast<uint64_t>(result.rc_));
  }
  return result;
}

OwnedImpl::OwnedImpl() : old_impl_(use_old_impl_) {
  if (old_impl_) {
    buffer_ = evbuffer_new();
  }
}

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

void OwnedImpl::appendSliceForTest(const void* data, uint64_t size) {
  if (old_impl_) {
    OwnedImpl rhs(data, size);
    move(rhs);
  } else {
    slices_.emplace_back(OwnedSlice::create(data, size));
    length_ += size;
  }
}

void OwnedImpl::appendSliceForTest(absl::string_view data) {
  appendSliceForTest(data.data(), data.size());
}

void OwnedImpl::useOldImpl(bool use_old_impl) { use_old_impl_ = use_old_impl; }

bool OwnedImpl::isSameBufferImpl(const Instance& rhs) const {
  const OwnedImpl* other = dynamic_cast<const OwnedImpl*>(&rhs);
  if (other == nullptr) {
    return false;
  }
  return usesOldImpl() == other->usesOldImpl();
}

bool OwnedImpl::use_old_impl_ = false;

} // namespace Buffer
} // namespace Envoy
