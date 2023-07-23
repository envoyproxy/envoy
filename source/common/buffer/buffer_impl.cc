#include "source/common/buffer/buffer_impl.h"

#include <cstdint>
#include <memory>
#include <string>

#include "source/common/common/assert.h"

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

thread_local absl::InlinedVector<Slice::StoragePtr,
                                 OwnedImpl::OwnedImplReservationSlicesOwnerMultiple::free_list_max_>
    OwnedImpl::OwnedImplReservationSlicesOwnerMultiple::free_list_;

void OwnedImpl::addImpl(const void* data, uint64_t size) {
  const char* src = static_cast<const char*>(data);
  bool new_slice_needed = slices_.empty();
  while (size != 0) {
    if (new_slice_needed) {
      slices_.emplace_back(Slice(size, account_));
    }
    uint64_t copy_size = slices_.back().append(src, size);
    src += copy_size;
    size -= copy_size;
    length_ += copy_size;
    new_slice_needed = true;
  }
}

void OwnedImpl::addDrainTracker(std::function<void()> drain_tracker) {
  ASSERT(!slices_.empty());
  slices_.back().addDrainTracker(std::move(drain_tracker));
}

void OwnedImpl::bindAccount(BufferMemoryAccountSharedPtr account) {
  ASSERT(slices_.empty());
  account_ = std::move(account);
}

BufferMemoryAccountSharedPtr OwnedImpl::getAccountForTest() { return account_; }

void OwnedImpl::add(const void* data, uint64_t size) { addImpl(data, size); }

void OwnedImpl::addBufferFragment(BufferFragment& fragment) {
  length_ += fragment.size();
  slices_.emplace_back(fragment);
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
      slices_.emplace_front(Slice(size, account_));
    }
    uint64_t copy_size = slices_.front().prepend(data.data(), size);
    size -= copy_size;
    length_ += copy_size;
    new_slice_needed = true;
  }
}

void OwnedImpl::prepend(Instance& data) {
  ASSERT(&data != this);
  OwnedImpl& other = static_cast<OwnedImpl&>(data);
  while (!other.slices_.empty()) {
    uint64_t slice_size = other.slices_.back().dataSize();
    length_ += slice_size;
    slices_.emplace_front(std::move(other.slices_.back()));
    slices_.front().maybeChargeAccount(account_);
    other.slices_.pop_back();
    other.length_ -= slice_size;
  }
  other.postProcess();
}

void OwnedImpl::copyOut(size_t start, uint64_t size, void* data) const {
  uint64_t bytes_to_skip = start;
  uint8_t* dest = static_cast<uint8_t*>(data);
  for (const auto& slice : slices_) {
    if (size == 0) {
      break;
    }
    uint64_t data_size = slice.dataSize();
    if (data_size <= bytes_to_skip) {
      // The offset where the caller wants to start copying is after the end of this slice,
      // so just skip over this slice completely.
      bytes_to_skip -= data_size;
      continue;
    }
    uint64_t copy_size = std::min(size, data_size - bytes_to_skip);
    memcpy(dest, slice.data() + bytes_to_skip, copy_size); // NOLINT(safe-memcpy)
    size -= copy_size;
    dest += copy_size;
    // Now that we've started copying, there are no bytes left to skip over. If there
    // is any more data to be copied, the next iteration can start copying from the very
    // beginning of the next slice.
    bytes_to_skip = 0;
  }
  ASSERT(size == 0);
}

uint64_t OwnedImpl::copyOutToSlices(uint64_t size, Buffer::RawSlice* dest_slices,
                                    uint64_t num_slice) const {
  uint64_t total_length_to_read = std::min(size, this->length());
  uint64_t num_bytes_read = 0;
  uint64_t num_dest_slices_read = 0;
  uint64_t num_src_slices_read = 0;
  uint64_t dest_slice_offset = 0;
  uint64_t src_slice_offset = 0;
  while (num_dest_slices_read < num_slice && num_bytes_read < total_length_to_read) {
    const Slice& src_slice = slices_[num_src_slices_read];
    const Buffer::RawSlice& dest_slice = dest_slices[num_dest_slices_read];
    uint64_t left_to_read = total_length_to_read - num_bytes_read;
    uint64_t left_data_size_in_dst_slice = dest_slice.len_ - dest_slice_offset;
    uint64_t left_data_size_in_src_slice = src_slice.dataSize() - src_slice_offset;
    // The length to copy should be size of smallest in the source slice available size and
    // the dest slice available size.
    uint64_t length_to_copy =
        std::min(left_data_size_in_src_slice, std::min(left_data_size_in_dst_slice, left_to_read));
    memcpy(static_cast<uint8_t*>(dest_slice.mem_) + dest_slice_offset, // NOLINT(safe-memcpy)
           src_slice.data() + src_slice_offset, length_to_copy);
    src_slice_offset = src_slice_offset + length_to_copy;
    dest_slice_offset = dest_slice_offset + length_to_copy;
    if (src_slice_offset == src_slice.dataSize()) {
      num_src_slices_read++;
      src_slice_offset = 0;
    }
    if (dest_slice_offset == dest_slice.len_) {
      num_dest_slices_read++;
      dest_slice_offset = 0;
    }
    ASSERT(src_slice_offset <= src_slice.dataSize());
    ASSERT(dest_slice_offset <= dest_slice.len_);
    num_bytes_read += length_to_copy;
  }
  return num_bytes_read;
}

void OwnedImpl::drain(uint64_t size) { drainImpl(size); }

void OwnedImpl::drainImpl(uint64_t size) {
  while (size != 0) {
    if (slices_.empty()) {
      break;
    }
    uint64_t slice_size = slices_.front().dataSize();
    if (slice_size <= size) {
      slices_.pop_front();
      length_ -= slice_size;
      size -= slice_size;
    } else {
      slices_.front().drain(size);
      length_ -= size;
      size = 0;
    }
  }
  // Make sure to drain any zero byte fragments that might have been added as
  // sentinels for flushed data.
  while (!slices_.empty() && slices_.front().dataSize() == 0) {
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

    if (slice.dataSize() == 0) {
      continue;
    }

    // Temporary cast to fix 32-bit Envoy mobile builds, where sizeof(uint64_t) != sizeof(size_t).
    // dataSize represents the size of a buffer so size_t should always be large enough to hold its
    // size regardless of architecture. Buffer slices should in practice be relatively small, but
    // there is currently no max size validation.
    // TODO(antoniovicente) Set realistic limits on the max size of BufferSlice and consider use of
    // size_t instead of uint64_t in the Slice interface.
    raw_slices.emplace_back(
        RawSlice{const_cast<uint8_t*>(slice.data()), static_cast<size_t>(slice.dataSize())});
  }
  return raw_slices;
}

RawSlice OwnedImpl::frontSlice() const {
  // Ignore zero-size slices and return the first slice with data.
  for (const auto& slice : slices_) {
    if (slice.dataSize() > 0) {
      return RawSlice{const_cast<uint8_t*>(slice.data()),
                      static_cast<absl::Span<uint8_t>::size_type>(slice.dataSize())};
    }
  }

  return {nullptr, 0};
}

SliceDataPtr OwnedImpl::extractMutableFrontSlice() {
  RELEASE_ASSERT(length_ > 0, "Extract called on empty buffer");
  // Remove zero byte fragments from the front of the queue to ensure
  // that the extracted slice has data.
  while (!slices_.empty() && slices_.front().dataSize() == 0) {
    slices_.pop_front();
  }
  ASSERT(!slices_.empty());
  auto slice = std::move(slices_.front());
  auto size = slice.dataSize();
  length_ -= size;
  slices_.pop_front();
  if (!slice.isMutable()) {
    // Create a mutable copy of the immutable slice data.
    Slice mutable_slice{size, nullptr};
    auto copy_size = mutable_slice.append(slice.data(), size);
    ASSERT(copy_size == size);
    // Drain trackers for the immutable slice will be called as part of the slice destructor.
    return std::make_unique<SliceDataImpl>(std::move(mutable_slice));
  } else {
    // Make sure drain trackers are called before ownership of the slice is transferred from
    // the buffer to the caller.
    slice.callAndClearDrainTrackersAndCharges();
    return std::make_unique<SliceDataImpl>(std::move(slice));
  }
}

uint64_t OwnedImpl::length() const {
#ifndef NDEBUG
  // When running in debug mode, verify that the precomputed length matches the sum
  // of the lengths of the slices.
  uint64_t length = 0;
  for (const auto& slice : slices_) {
    length += slice.dataSize();
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
  if (slices_[0].dataSize() < size) {
    Slice new_slice{size, account_};
    Slice::Reservation reservation = new_slice.reserve(size);
    ASSERT(reservation.mem_ != nullptr);
    ASSERT(reservation.len_ == size);
    copyOut(0, size, reservation.mem_);
    new_slice.commit(reservation);

    // Replace the first 'size' bytes in the buffer with the new slice. Since new_slice re-adds the
    // drained bytes, avoid use of the overridable 'drain' method to avoid incorrectly checking if
    // we dipped below low-watermark.
    drainImpl(size);
    slices_.emplace_front(std::move(new_slice));
    length_ += size;
  }
  return slices_.front().data();
}

void OwnedImpl::coalesceOrAddSlice(Slice&& other_slice) {
  const uint64_t slice_size = other_slice.dataSize();
  // The `other_slice` content can be coalesced into the existing slice IFF:
  // 1. The `other_slice` can be coalesced. Immutable slices can not be safely coalesced because
  // their destructors can be arbitrary global side effects.
  // 2. There are existing slices;
  // 3. The `other_slice` content length is under the CopyThreshold;
  // 4. There is enough unused space in the existing slice to accommodate the `other_slice` content.
  if (other_slice.canCoalesce() && !slices_.empty() && slice_size < CopyThreshold &&
      slices_.back().reservableSize() >= slice_size) {
    // Copy content of the `other_slice`. The `move` methods which call this method effectively
    // drain the source buffer.
    addImpl(other_slice.data(), slice_size);
    other_slice.transferDrainTrackersTo(slices_.back());
  } else {
    // Take ownership of the slice.
    other_slice.maybeChargeAccount(account_);
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
    const uint64_t slice_size = other.slices_.front().dataSize();
    coalesceOrAddSlice(std::move(other.slices_.front()));
    other.length_ -= slice_size;
    other.slices_.pop_front();
  }
  other.postProcess();
}

void OwnedImpl::move(Instance& rhs, uint64_t length) {
  move(rhs, length, /*reset_drain_trackers_and_accounting=*/false);
}

void OwnedImpl::move(Instance& rhs, uint64_t length, bool reset_drain_trackers_and_accounting) {
  ASSERT(&rhs != this);
  // See move() above for why we do the static cast.
  OwnedImpl& other = static_cast<OwnedImpl&>(rhs);
  while (length != 0 && !other.slices_.empty()) {
    const uint64_t slice_size = other.slices_.front().dataSize();
    const uint64_t copy_size = std::min(slice_size, length);
    if (copy_size == 0) {
      other.slices_.pop_front();
    } else if (copy_size < slice_size) {
      // TODO(brian-pane) add reference-counting to allow slices to share their storage
      // and eliminate the copy for this partial-slice case?
      add(other.slices_.front().data(), copy_size);
      other.slices_.front().drain(copy_size);
      other.length_ -= copy_size;
    } else {
      if (reset_drain_trackers_and_accounting) {
        // The other slice is owned by a user-space IO handle and its drain trackers may refer to a
        // connection that can die (and be freed) at any time. Call and clear the drain trackers to
        // avoid potential use-after-free.
        other.slices_.front().callAndClearDrainTrackersAndCharges();
      }
      coalesceOrAddSlice(std::move(other.slices_.front()));
      other.slices_.pop_front();
      other.length_ -= slice_size;
    }
    length -= copy_size;
  }
  other.postProcess();
}

Reservation OwnedImpl::reserveForRead() {
  return reserveWithMaxLength(default_read_reservation_size_);
}

Reservation OwnedImpl::reserveWithMaxLength(uint64_t max_length) {
  Reservation reservation = Reservation::bufferImplUseOnlyConstruct(*this);
  if (max_length == 0) {
    return reservation;
  }

  // Remove any empty slices at the end.
  while (!slices_.empty() && slices_.back().dataSize() == 0) {
    slices_.pop_back();
  }

  uint64_t bytes_remaining = max_length;
  uint64_t reserved = 0;
  auto& reservation_slices = reservation.bufferImplUseOnlySlices();
  auto slices_owner = std::make_unique<OwnedImplReservationSlicesOwnerMultiple>();

  // Check whether there are any empty slices with reservable space at the end of the buffer.
  uint64_t reservable_size = slices_.empty() ? 0 : slices_.back().reservableSize();
  if (reservable_size >= max_length || reservable_size >= (Slice::default_slice_size_ / 8)) {
    uint64_t reserve_size = std::min(reservable_size, bytes_remaining);
    RawSlice slice = slices_.back().reserve(reserve_size);
    reservation_slices.push_back(slice);
    slices_owner->owned_storages_.push_back({});
    bytes_remaining -= slice.len_;
    reserved += slice.len_;
  }

  while (bytes_remaining != 0 && reservation_slices.size() < reservation.MAX_SLICES_) {
    constexpr uint64_t size = Slice::default_slice_size_;

    // If the next slice would go over the desired size, and the amount already reserved is already
    // at least one full slice in size, stop allocating slices. This prevents returning a
    // reservation larger than requested, which could go above the watermark limits for a watermark
    // buffer, unless the size would be very small (less than 1 full slice).
    if (size > bytes_remaining && reserved >= size) {
      break;
    }

    Slice::SizedStorage storage = slices_owner->newStorage();
    ASSERT(storage.len_ == size);
    const RawSlice raw_slice{storage.mem_.get(), size};
    slices_owner->owned_storages_.emplace_back(std::move(storage));
    reservation_slices.push_back(raw_slice);
    bytes_remaining -= std::min<uint64_t>(raw_slice.len_, bytes_remaining);
    reserved += raw_slice.len_;
  }

  ASSERT(reservation_slices.size() == slices_owner->owned_storages_.size());
  reservation.bufferImplUseOnlySlicesOwner() = std::move(slices_owner);
  reservation.bufferImplUseOnlySetLength(reserved);

  return reservation;
}

ReservationSingleSlice OwnedImpl::reserveSingleSlice(uint64_t length, bool separate_slice) {
  ReservationSingleSlice reservation = ReservationSingleSlice::bufferImplUseOnlyConstruct(*this);
  if (length == 0) {
    return reservation;
  }

  // Remove any empty slices at the end.
  while (!slices_.empty() && slices_.back().dataSize() == 0) {
    slices_.pop_back();
  }

  auto& reservation_slice = reservation.bufferImplUseOnlySlice();
  auto slice_owner = std::make_unique<OwnedImplReservationSlicesOwnerSingle>();

  // Check whether there are any empty slices with reservable space at the end of the buffer.
  uint64_t reservable_size =
      (separate_slice || slices_.empty()) ? 0 : slices_.back().reservableSize();
  if (reservable_size >= length) {
    reservation_slice = slices_.back().reserve(length);
  } else {
    slice_owner->owned_storage_ = Slice::newStorage(length);
    ASSERT(slice_owner->owned_storage_.len_ >= length);
    reservation_slice = {slice_owner->owned_storage_.mem_.get(), static_cast<size_t>(length)};
  }

  reservation.bufferImplUseOnlySliceOwner() = std::move(slice_owner);

  return reservation;
}

void OwnedImpl::commit(uint64_t length, absl::Span<RawSlice> slices,
                       ReservationSlicesOwnerPtr slices_owner_base) {
  if (length == 0) {
    return;
  }

  ASSERT(dynamic_cast<OwnedImplReservationSlicesOwner*>(slices_owner_base.get()) != nullptr);
  std::unique_ptr<OwnedImplReservationSlicesOwner> slices_owner(
      static_cast<OwnedImplReservationSlicesOwner*>(slices_owner_base.release()));

  absl::Span<Slice::SizedStorage> owned_storages = slices_owner->ownedStorages();
  ASSERT(slices.size() == owned_storages.size());

  uint64_t bytes_remaining = length;
  for (uint32_t i = 0; i < slices.size() && bytes_remaining > 0; i++) {
    slices[i].len_ = std::min<uint64_t>(slices[i].len_, bytes_remaining);

    if (auto& owned_storage = owned_storages[i]; owned_storage.mem_ != nullptr) {
      ASSERT(slices[i].len_ <= owned_storage.len_);
      slices_.emplace_back(Slice(std::move(owned_storage), slices[i].len_, account_));
    } else {
      bool success = slices_.back().commit<false>(slices[i]);
      ASSERT(success);
    }

    length_ += slices[i].len_;
    bytes_remaining -= slices[i].len_;
  }
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
    uint64_t slice_size = slice.dataSize();
    if (slice_size <= start) {
      start -= slice_size;
      offset += slice_size;
      continue;
    }
    const uint8_t* slice_start = slice.data();
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
          match_next = match_slice.data();
          match_end = match_next + match_slice.dataSize();
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
    uint64_t slice_size = slice.dataSize();
    const uint8_t* slice_start = slice.data();

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
  IS_ENVOY_BUG("unexpected data in slices");
  return false;
}

OwnedImpl::OwnedImpl() = default;

OwnedImpl::OwnedImpl(absl::string_view data) : OwnedImpl() { add(data); }

OwnedImpl::OwnedImpl(const Instance& data) : OwnedImpl() { add(data); }

OwnedImpl::OwnedImpl(const void* data, uint64_t size) : OwnedImpl() { add(data, size); }

OwnedImpl::OwnedImpl(BufferMemoryAccountSharedPtr account) : account_(std::move(account)) {}

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
  slices_.emplace_back(Slice(size, account_));
  slices_.back().append(data, size);
  length_ += size;
}

void OwnedImpl::appendSliceForTest(absl::string_view data) {
  appendSliceForTest(data.data(), data.size());
}

std::vector<Slice::SliceRepresentation> OwnedImpl::describeSlicesForTest() const {
  std::vector<Slice::SliceRepresentation> slices;
  for (const auto& slice : slices_) {
    slices.push_back(slice.describeSliceForTest());
  }
  return slices;
}

size_t OwnedImpl::addFragments(absl::Span<const absl::string_view> fragments) {
  size_t total_size_to_copy = 0;

  for (const auto& fragment : fragments) {
    total_size_to_copy += fragment.size();
  }

  if (slices_.empty()) {
    slices_.emplace_back(Slice(total_size_to_copy, account_));
  }

  Slice& back = slices_.back();
  Slice::Reservation reservation = back.reserve(total_size_to_copy);
  uint8_t* mem = static_cast<uint8_t*>(reservation.mem_);
  if (reservation.len_ == total_size_to_copy) {
    // Enough continuous memory for all fragments in the back slice then copy
    // all fragments directly for performance improvement.
    for (const auto& fragment : fragments) {
      memcpy(mem, fragment.data(), fragment.size()); // NOLINT(safe-memcpy)
      mem += fragment.size();
    }
    back.commit<false>(reservation);
    length_ += total_size_to_copy;
  } else {
    // Downgrade to using `addImpl` if not enough memory in the back slice.
    // TODO(wbpcode): Fill the remaining memory space in the back slice then
    // allocate enough contiguous memory for the remaining unwritten fragments
    // and copy them directly. This may result in better performance.
    for (const auto& fragment : fragments) {
      addImpl(fragment.data(), fragment.size());
    }
  }

  return total_size_to_copy;
}

} // namespace Buffer
} // namespace Envoy
