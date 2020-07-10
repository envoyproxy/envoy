#include "common/buffer/buffer_impl.h"

#include <cstdint>
#include <string>

#include "common/common/assert.h"

#include "absl/container/fixed_array.h"
#include "event2/buffer.h"

namespace Envoy {
namespace Buffer {
namespace {
// This size has been determined to be optimal from running the
// //test/integration:http_benchmark benchmark tests.
// TODO(yanavlasov): This may not be optimal for all hardware configurations or traffic patterns and
// may need to be configurable in the future.
constexpr uint64_t CopyThreshold = 512;
} // namespace

void OwnedImpl::addImpl(const void* data, uint64_t size) {
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

void OwnedImpl::addDrainTracker(std::function<void()> drain_tracker) {
  ASSERT(!slices_.empty());
  slices_.back()->addDrainTracker(std::move(drain_tracker));
}

void OwnedImpl::add(const void* data, uint64_t size) { addImpl(data, size); }

void OwnedImpl::addBufferFragment(BufferFragment& fragment) {
  length_ += fragment.size();
  slices_.emplace_back(std::make_unique<UnownedSlice>(fragment));
}

void OwnedImpl::add(absl::string_view data) { add(data.data(), data.size()); }

void OwnedImpl::add(const Instance& data) {
  ASSERT(&data != this);
  for (const RawSlice& slice : data.getRawSlices()) {
    add(slice.mem_, slice.len_);
  }
}

void OwnedImpl::prepend(absl::string_view data) {
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

void OwnedImpl::prepend(Instance& data) {
  ASSERT(&data != this);
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

  // In case an extra slice was reserved, remove empty slices from the end of the buffer.
  while (!slices_.empty() && slices_.back()->dataSize() == 0) {
    slices_.pop_back();
  }

  ASSERT(num_slices_committed > 0);
}

void OwnedImpl::copyOut(size_t start, uint64_t size, void* data) const {
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
  // Make sure to drain any zero byte fragments that might have been added as
  // sentinels for flushed data.
  while (!slices_.empty() && slices_.front()->dataSize() == 0) {
    slices_.pop_front();
  }
}

RawSliceVector OwnedImpl::getRawSlices(absl::optional<uint64_t> max_slices) const {
  uint64_t max_out = slices_.size();
  if (max_slices.has_value()) {
    max_out = std::min(max_out, max_slices.value());
  }

  RawSliceVector raw_slices;
  raw_slices.reserve(max_out);
  for (const auto& slice : slices_) {
    if (raw_slices.size() >= max_out) {
      break;
    }

    if (slice->dataSize() == 0) {
      continue;
    }

    // Temporary cast to fix 32-bit Envoy mobile builds, where sizeof(uint64_t) != sizeof(size_t).
    // dataSize represents the size of a buffer so size_t should always be large enough to hold its
    // size regardless of architecture. Buffer slices should in practice be relatively small, but
    // there is currently no max size validation.
    // TODO(antoniovicente) Set realistic limits on the max size of BufferSlice and consider use of
    // size_t instead of uint64_t in the Slice interface.
    raw_slices.emplace_back(RawSlice{slice->data(), static_cast<size_t>(slice->dataSize())});
  }
  return raw_slices;
}

uint64_t OwnedImpl::length() const {
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

void* OwnedImpl::linearize(uint32_t size) {
  RELEASE_ASSERT(size <= length(), "Linearize size exceeds buffer size");
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
      if (data_size > 0) {
        memcpy(dest, slices_.front()->data(), data_size);
        bytes_copied += data_size;
        dest += data_size;
      }
      slices_.pop_front();
    } while (bytes_copied < linearized_size);
    ASSERT(dest == static_cast<const uint8_t*>(reservation.mem_) + linearized_size);
    new_slice->commit(reservation);
    slices_.emplace_front(std::move(new_slice));
  }
  return slices_.front()->data();
}

void OwnedImpl::coalesceOrAddSlice(SlicePtr&& other_slice) {
  const uint64_t slice_size = other_slice->dataSize();
  // The `other_slice` content can be coalesced into the existing slice IFF:
  // 1. The `other_slice` can be coalesced. Objects of type UnownedSlice can not be coalesced. See
  //    comment in the UnownedSlice class definition;
  // 2. There are existing slices;
  // 3. The `other_slice` content length is under the CopyThreshold;
  // 4. There is enough unused space in the existing slice to accommodate the `other_slice` content.
  if (other_slice->canCoalesce() && !slices_.empty() && slice_size < CopyThreshold &&
      slices_.back()->reservableSize() >= slice_size) {
    // Copy content of the `other_slice`. The `move` methods which call this method effectively
    // drain the source buffer.
    addImpl(other_slice->data(), slice_size);
    other_slice->transferDrainTrackersTo(*slices_.back());
  } else {
    // Take ownership of the slice.
    slices_.emplace_back(std::move(other_slice));
    length_ += slice_size;
  }
}

void OwnedImpl::move(Instance& rhs) {
  ASSERT(&rhs != this);
  // We do the static cast here because in practice we only have one buffer implementation right
  // now and this is safe. This is a reasonable compromise in a high performance path where we
  // want to maintain an abstraction.
  OwnedImpl& other = static_cast<OwnedImpl&>(rhs);
  while (!other.slices_.empty()) {
    const uint64_t slice_size = other.slices_.front()->dataSize();
    coalesceOrAddSlice(std::move(other.slices_.front()));
    other.length_ -= slice_size;
    other.slices_.pop_front();
  }
  other.postProcess();
}

void OwnedImpl::move(Instance& rhs, uint64_t length) {
  ASSERT(&rhs != this);
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
      coalesceOrAddSlice(std::move(other.slices_.front()));
      other.slices_.pop_front();
      other.length_ -= slice_size;
    }
    length -= copy_size;
  }
  other.postProcess();
}

Api::IoCallUint64Result OwnedImpl::read(Network::IoHandle& io_handle, uint64_t max_length) {
  if (max_length == 0) {
    return Api::ioCallUint64ResultNoError();
  }
  constexpr uint64_t MaxSlices = 2;
  RawSlice slices[MaxSlices];
  const uint64_t num_slices = reserve(max_length, slices, MaxSlices);
  Api::IoCallUint64Result result = io_handle.readv(max_length, slices, num_slices);
  uint64_t bytes_to_commit = result.ok() ? result.rc_ : 0;
  ASSERT(bytes_to_commit <= max_length);
  for (uint64_t i = 0; i < num_slices; i++) {
    slices[i].len_ = std::min(slices[i].len_, static_cast<size_t>(bytes_to_commit));
    bytes_to_commit -= slices[i].len_;
  }
  commit(slices, num_slices);
  return result;
}

uint64_t OwnedImpl::reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) {
  if (num_iovecs == 0 || length == 0) {
    return 0;
  }
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

ssize_t OwnedImpl::search(const void* data, uint64_t size, size_t start, size_t length) const {
  // This implementation uses the same search algorithm as evbuffer_search(), a naive
  // scan that requires O(M*N) comparisons in the worst case.
  // TODO(brian-pane): replace this with a more efficient search if it shows up
  // prominently in CPU profiling.
  if (size == 0) {
    return (start <= length_) ? start : -1;
  }

  // length equal to zero means that entire buffer must be searched.
  // Adjust the length to buffer length taking the staring index into account.
  size_t left_to_search = length;
  if (0 == length) {
    left_to_search = length_ - start;
  }
  ssize_t offset = 0;
  const uint8_t* needle = static_cast<const uint8_t*>(data);
  for (size_t slice_index = 0; slice_index < slices_.size() && (left_to_search > 0);
       slice_index++) {
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
      const size_t slice_search_limit =
          std::min(static_cast<size_t>(haystack_end - haystack), left_to_search);
      // Search within this slice for the first byte of the needle.
      const uint8_t* first_byte_match =
          static_cast<const uint8_t*>(memchr(haystack, needle[0], slice_search_limit));
      if (first_byte_match == nullptr) {
        left_to_search -= slice_search_limit;
        break;
      }
      // After finding a match for the first byte of the needle, check whether the following
      // bytes in the buffer match the remainder of the needle. Note that the match can span
      // two or more slices.
      left_to_search -= static_cast<size_t>(first_byte_match - haystack + 1);
      // Save the current number of bytes left to search.
      // If the pattern is not found, the search will resume from the next byte
      // and left_to_search value must be restored.
      const size_t saved_left_to_search = left_to_search;
      size_t i = 1;
      size_t match_index = slice_index;
      const uint8_t* match_next = first_byte_match + 1;
      const uint8_t* match_end = haystack_end;
      while ((i < size) && (0 < left_to_search)) {
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
        left_to_search--;
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
      left_to_search = saved_left_to_search;
    }
    start = 0;
    offset += slice_size;
  }
  return -1;
}

bool OwnedImpl::startsWith(absl::string_view data) const {
  if (length() < data.length()) {
    // Buffer is too short to contain data.
    return false;
  }

  if (data.length() == 0) {
    return true;
  }

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

Api::IoCallUint64Result OwnedImpl::write(Network::IoHandle& io_handle) {
  constexpr uint64_t MaxSlices = 16;
  RawSliceVector slices = getRawSlices(MaxSlices);
  Api::IoCallUint64Result result = io_handle.writev(slices.begin(), slices.size());
  if (result.ok() && result.rc_ > 0) {
    drain(static_cast<uint64_t>(result.rc_));
  }
  return result;
}

OwnedImpl::OwnedImpl() = default;

OwnedImpl::OwnedImpl(absl::string_view data) : OwnedImpl() { add(data); }

OwnedImpl::OwnedImpl(const Instance& data) : OwnedImpl() { add(data); }

OwnedImpl::OwnedImpl(const void* data, uint64_t size) : OwnedImpl() { add(data, size); }

std::string OwnedImpl::toString() const {
  std::string output;
  output.reserve(length());
  for (const RawSlice& slice : getRawSlices()) {
    output.append(static_cast<const char*>(slice.mem_), slice.len_);
  }

  return output;
}

void OwnedImpl::postProcess() {}

void OwnedImpl::appendSliceForTest(const void* data, uint64_t size) {
  slices_.emplace_back(OwnedSlice::create(data, size));
  length_ += size;
}

void OwnedImpl::appendSliceForTest(absl::string_view data) {
  appendSliceForTest(data.data(), data.size());
}

std::vector<OwnedSlice::SliceRepresentation> OwnedImpl::describeSlicesForTest() const {
  std::vector<OwnedSlice::SliceRepresentation> slices;
  for (const auto& slice : slices_) {
    slices.push_back(slice->describeSliceForTest());
  }
  return slices;
}

} // namespace Buffer
} // namespace Envoy
