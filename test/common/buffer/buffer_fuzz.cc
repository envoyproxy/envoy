#include "test/common/buffer/buffer_fuzz.h"

#include <fcntl.h>

#include "envoy/common/platform.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/memory/stats.h"
#include "common/network/io_socket_handle_impl.h"

#include "test/fuzz/utility.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/match.h"
#include "gtest/gtest.h"

namespace Envoy {

namespace {

// The number of buffers tracked. Each buffer fuzzer action references one or
// more of these. We don't need a ton of buffers to capture the range of
// possible behaviors, at least two to properly model move operations, let's
// assume only 3 for now.
constexpr uint32_t BufferCount = 3;

// These data are exogenous to the buffer, we don't need to worry about their
// deallocation, just keep them around until the fuzz run is over.
struct Context {
  std::vector<std::unique_ptr<Buffer::BufferFragmentImpl>> fragments_;
};

// Bound the maximum allocation size per action. We want this to be able to at
// least cover the span of multiple internal chunks. It looks like both
// the new OwnedImpl and libevent have minimum chunks in O(a few kilobytes).
// This makes sense in general, since you need to to minimize data structure
// overhead. If we make this number too big, we risk spending a lot of time in
// memcpy/memcmp and slowing down the fuzzer execution rate. The number below is
// our current best compromise.
constexpr uint32_t MaxAllocation = 16 * 1024;

// Hard bound on total bytes allocated across the trace.
constexpr uint32_t TotalMaxAllocation = 4 * MaxAllocation;

uint32_t clampSize(uint32_t size, uint32_t max_alloc) {
  return std::min(size, std::min(MaxAllocation, max_alloc));
}

void releaseFragmentAllocation(const void* p, size_t, const Buffer::BufferFragmentImpl*) {
  ::free(const_cast<void*>(p));
}

// Test implementation of Buffer. Conceptually, this is just a string that we
// can append/prepend to and consume bytes from the front of. However, naive
// implementations with std::string involve lots of copying to support this, and
// even std::stringbuf doesn't support cheap linearization. Instead we use a
// flat array that takes advantage of the fact that the total number of bytes
// allocated during fuzzing will be bounded by TotalMaxAllocation.
//
// The data structure is built around the concept of a large flat array of size
// 2 * TotalMaxAllocation, with the initial start position set to the middle.
// The goal is to make every mutating operation linear time, including
// add() and prepend(), as well as supporting O(1) linearization (critical to
// making it cheaper to compare results with the real buffer implementation).
// We maintain a (start, length) pair and ensure via assertions that we never
// walk off the edge; the caller should be guaranteeing this.
class StringBuffer : public Buffer::Instance {
public:
  void addDrainTracker(std::function<void()> drain_tracker) override {
    // Not implemented well.
    ASSERT(false);
    drain_tracker();
  }

  void add(const void* data, uint64_t size) override {
    FUZZ_ASSERT(start_ + size_ + size <= data_.size());
    ::memcpy(mutableEnd(), data, size);
    size_ += size;
  }

  void addBufferFragment(Buffer::BufferFragment& fragment) override {
    add(fragment.data(), fragment.size());
    fragment.done();
  }

  void add(absl::string_view data) override { add(data.data(), data.size()); }

  void add(const Buffer::Instance& data) override {
    const StringBuffer& src = dynamic_cast<const StringBuffer&>(data);
    add(src.start(), src.size_);
  }

  void prepend(absl::string_view data) override {
    FUZZ_ASSERT(start_ >= data.size());
    start_ -= data.size();
    size_ += data.size();
    ::memcpy(mutableStart(), data.data(), data.size());
  }

  void prepend(Instance& data) override {
    StringBuffer& src = dynamic_cast<StringBuffer&>(data);
    prepend(src.asStringView());
    src.size_ = 0;
  }

  void copyOut(size_t start, uint64_t size, void* data) const override {
    ::memcpy(data, this->start() + start, size);
  }

  void drain(uint64_t size) override {
    FUZZ_ASSERT(size <= size_);
    start_ += size;
    size_ -= size;
  }

  Buffer::RawSliceVector
  getRawSlices(absl::optional<uint64_t> max_slices = absl::nullopt) const override {
    ASSERT(!max_slices.has_value() || max_slices.value() >= 1);
    return {{const_cast<char*>(start()), size_}};
  }

  Buffer::RawSlice frontSlice() const override { return {const_cast<char*>(start()), size_}; }

  uint64_t length() const override { return size_; }

  void* linearize(uint32_t /*size*/) override {
    // Sketchy, but probably will work for test purposes.
    return mutableStart();
  }

  Buffer::SliceDataPtr extractMutableFrontSlice() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

  void move(Buffer::Instance& rhs) override { move(rhs, rhs.length()); }

  void move(Buffer::Instance& rhs, uint64_t length) override {
    StringBuffer& src = dynamic_cast<StringBuffer&>(rhs);
    add(src.start(), length);
    src.start_ += length;
    src.size_ -= length;
  }

  Buffer::Reservation reserveForRead() override {
    auto reservation = Buffer::Reservation::bufferImplUseOnlyConstruct(*this);
    Buffer::RawSlice slice;
    slice.mem_ = mutableEnd();
    slice.len_ = data_.size() - (start_ + size_);
    reservation.bufferImplUseOnlySlices().push_back(slice);
    reservation.bufferImplUseOnlySetLength(slice.len_);

    return reservation;
  }

  Buffer::ReservationSingleSlice reserveSingleSlice(uint64_t length, bool separate_slice) override {
    ASSERT(!separate_slice);
    FUZZ_ASSERT(start_ + size_ + length <= data_.size());

    auto reservation = Buffer::ReservationSingleSlice::bufferImplUseOnlyConstruct(*this);
    Buffer::RawSlice slice;
    slice.mem_ = mutableEnd();
    slice.len_ = length;
    reservation.bufferImplUseOnlySlice() = slice;

    return reservation;
  }

  void commit(uint64_t length, absl::Span<Buffer::RawSlice>,
              Buffer::ReservationSlicesOwnerPtr) override {
    size_ += length;
    FUZZ_ASSERT(start_ + size_ + length <= data_.size());
  }

  ssize_t search(const void* data, uint64_t size, size_t start, size_t length) const override {
    UNREFERENCED_PARAMETER(length);
    return asStringView().find({static_cast<const char*>(data), size}, start);
  }

  bool startsWith(absl::string_view data) const override {
    return absl::StartsWith(asStringView(), data);
  }

  std::string toString() const override { return std::string(data_.data() + start_, size_); }

  void setWatermarks(uint32_t) override {
    // Not implemented.
    // TODO(antoniovicente) Implement and add fuzz coverage as we merge the Buffer::OwnedImpl and
    // WatermarkBuffer implementations.
    ASSERT(false);
  }
  uint32_t highWatermark() const override { return 0; }
  bool highWatermarkTriggered() const override { return false; }

  absl::string_view asStringView() const { return {start(), size_}; }

  char* mutableStart() { return data_.data() + start_; }

  const char* start() const { return data_.data() + start_; }

  char* mutableEnd() { return mutableStart() + size_; }

  const char* end() const { return start() + size_; }

  std::array<char, 2 * TotalMaxAllocation> data_;
  uint32_t start_{TotalMaxAllocation};
  uint32_t size_{0};
};

using BufferList = std::vector<std::unique_ptr<Buffer::Instance>>;

// Process a single buffer operation.
uint32_t bufferAction(Context& ctxt, char insert_value, uint32_t max_alloc, BufferList& buffers,
                      const test::common::buffer::Action& action) {
  const uint32_t target_index = action.target_index() % BufferCount;
  Buffer::Instance& target_buffer = *buffers[target_index];
  uint32_t allocated = 0;

  switch (action.action_selector_case()) {
  case test::common::buffer::Action::kAddBufferFragment: {
    const uint32_t size = clampSize(action.add_buffer_fragment(), max_alloc);
    allocated += size;
    void* p = ::malloc(size);
    FUZZ_ASSERT(p != nullptr);
    ::memset(p, insert_value, size);
    auto fragment =
        std::make_unique<Buffer::BufferFragmentImpl>(p, size, releaseFragmentAllocation);
    ctxt.fragments_.emplace_back(std::move(fragment));
    target_buffer.addBufferFragment(*ctxt.fragments_.back());
    break;
  }
  case test::common::buffer::Action::kAddString: {
    const uint32_t size = clampSize(action.add_string(), max_alloc);
    allocated += size;
    const std::string data(size, insert_value);
    target_buffer.add(data);
    break;
  }
  case test::common::buffer::Action::kAddBuffer: {
    const uint32_t source_index = action.add_buffer() % BufferCount;
    if (target_index == source_index) {
      break;
    }
    Buffer::Instance& source_buffer = *buffers[source_index];
    if (source_buffer.length() > max_alloc) {
      break;
    }
    allocated += source_buffer.length();
    target_buffer.add(source_buffer);
    break;
  }
  case test::common::buffer::Action::kPrependString: {
    const uint32_t size = clampSize(action.prepend_string(), max_alloc);
    allocated += size;
    const std::string data(size, insert_value);
    target_buffer.prepend(data);
    break;
  }
  case test::common::buffer::Action::kPrependBuffer: {
    const uint32_t source_index = action.prepend_buffer() % BufferCount;
    if (target_index == source_index) {
      break;
    }
    Buffer::Instance& source_buffer = *buffers[source_index];
    if (source_buffer.length() > max_alloc) {
      break;
    }
    allocated += source_buffer.length();
    target_buffer.prepend(source_buffer);
    break;
  }
  case test::common::buffer::Action::kReserveCommit: {
    const uint32_t reserve_length = clampSize(action.reserve_commit().reserve_length(), max_alloc);
    allocated += reserve_length;
    if (reserve_length == 0) {
      break;
    }
    if (reserve_length < 16384) {
      auto reservation = target_buffer.reserveSingleSlice(reserve_length);
      ::memset(reservation.slice().mem_, insert_value, reservation.slice().len_);
      reservation.commit(
          std::min<uint64_t>(action.reserve_commit().commit_length(), reservation.length()));
    } else {
      Buffer::Reservation reservation = target_buffer.reserveForRead();
      for (uint32_t i = 0; i < reservation.numSlices(); ++i) {
        ::memset(reservation.slices()[i].mem_, insert_value, reservation.slices()[i].len_);
      }
      const uint32_t target_length =
          std::min<uint32_t>(reservation.length(), action.reserve_commit().commit_length());
      reservation.commit(target_length);
    }
    break;
  }
  case test::common::buffer::Action::kCopyOut: {
    const uint32_t start =
        std::min(action.copy_out().start(), static_cast<uint32_t>(target_buffer.length()));
    const uint32_t length =
        std::min(static_cast<uint32_t>(target_buffer.length() - start), action.copy_out().length());
    // Make this static to avoid potential continuous ASAN inspired allocation.
    static uint8_t copy_buffer[TotalMaxAllocation];
    target_buffer.copyOut(start, length, copy_buffer);
    const std::string data = target_buffer.toString();
    FUZZ_ASSERT(::memcmp(copy_buffer, data.data() + start, length) == 0);
    break;
  }
  case test::common::buffer::Action::kDrain: {
    const uint32_t previous_length = target_buffer.length();
    const uint32_t drain_length =
        std::min(static_cast<uint32_t>(target_buffer.length()), action.drain());
    target_buffer.drain(drain_length);
    FUZZ_ASSERT(previous_length - drain_length == target_buffer.length());
    break;
  }
  case test::common::buffer::Action::kLinearize: {
    const uint32_t linearize_size =
        std::min(static_cast<uint32_t>(target_buffer.length()), action.linearize());
    target_buffer.linearize(linearize_size);
    break;
  }
  case test::common::buffer::Action::kMove: {
    const uint32_t source_index = action.move().source_index() % BufferCount;
    if (target_index == source_index) {
      break;
    }
    Buffer::Instance& source_buffer = *buffers[source_index];
    if (action.move().length() == 0) {
      if (source_buffer.length() > max_alloc) {
        break;
      }
      allocated += source_buffer.length();
      target_buffer.move(source_buffer);
    } else {
      const uint32_t source_length =
          std::min(static_cast<uint32_t>(source_buffer.length()), action.move().length());
      const uint32_t move_length = clampSize(max_alloc, source_length);
      if (move_length == 0) {
        break;
      }
      target_buffer.move(source_buffer, move_length);
      allocated += move_length;
    }
    break;
  }
  case test::common::buffer::Action::kRead: {
    const uint32_t max_length = clampSize(action.read(), max_alloc);
    allocated += max_length;
    if (max_length == 0) {
      break;
    }
    int pipe_fds[2] = {0, 0};
    FUZZ_ASSERT(::pipe(pipe_fds) == 0);
    Network::IoSocketHandleImpl io_handle(pipe_fds[0]);
    FUZZ_ASSERT(::fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK) == 0);
    FUZZ_ASSERT(::fcntl(pipe_fds[1], F_SETFL, O_NONBLOCK) == 0);
    std::string data(max_length, insert_value);
    const ssize_t rc = ::write(pipe_fds[1], data.data(), max_length);
    FUZZ_ASSERT(rc > 0);
    Api::IoCallUint64Result result = io_handle.read(target_buffer, max_length);
    FUZZ_ASSERT(result.rc_ == static_cast<uint64_t>(rc));
    FUZZ_ASSERT(::close(pipe_fds[1]) == 0);
    break;
  }
  case test::common::buffer::Action::kWrite: {
    int pipe_fds[2] = {0, 0};
    FUZZ_ASSERT(::pipe(pipe_fds) == 0);
    Network::IoSocketHandleImpl io_handle(pipe_fds[1]);
    FUZZ_ASSERT(::fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK) == 0);
    FUZZ_ASSERT(::fcntl(pipe_fds[1], F_SETFL, O_NONBLOCK) == 0);
    uint64_t rc;
    do {
      const bool empty = target_buffer.length() == 0;
      const std::string previous_data = target_buffer.toString();
      const auto result = io_handle.write(target_buffer);
      FUZZ_ASSERT(result.ok());
      rc = result.rc_;
      ENVOY_LOG_MISC(trace, "Write rc: {} errno: {}", rc,
                     result.err_ != nullptr ? result.err_->getErrorDetails() : "-");
      if (empty) {
        FUZZ_ASSERT(rc == 0);
      } else {
        auto buf = std::make_unique<char[]>(rc);
        FUZZ_ASSERT(static_cast<uint64_t>(::read(pipe_fds[0], buf.get(), rc)) == rc);
        FUZZ_ASSERT(::memcmp(buf.get(), previous_data.data(), rc) == 0);
      }
    } while (rc > 0);
    FUZZ_ASSERT(::close(pipe_fds[0]) == 0);
    break;
  }
  case test::common::buffer::Action::kGetRawSlices: {
    const uint64_t slices_needed = target_buffer.getRawSlices().size();
    const uint64_t slices_tested =
        std::min(slices_needed, static_cast<uint64_t>(action.get_raw_slices()));
    if (slices_tested == 0) {
      break;
    }
    Buffer::RawSliceVector raw_slices = target_buffer.getRawSlices(/*max_slices=*/slices_tested);
    const uint64_t slices_obtained = raw_slices.size();
    FUZZ_ASSERT(slices_obtained <= slices_needed);
    uint64_t offset = 0;
    const std::string data = target_buffer.toString();
    for (const auto& raw_slices : raw_slices) {
      FUZZ_ASSERT(::memcmp(raw_slices.mem_, data.data() + offset, raw_slices.len_) == 0);
      offset += raw_slices.len_;
    }
    FUZZ_ASSERT(slices_needed != slices_tested || offset == target_buffer.length());
    break;
  }
  case test::common::buffer::Action::kSearch: {
    const std::string& content = action.search().content();
    const uint32_t offset = action.search().offset();
    const std::string data = target_buffer.toString();
    FUZZ_ASSERT(target_buffer.search(content.data(), content.size(), offset) ==
                static_cast<ssize_t>(target_buffer.toString().find(content, offset)));
    break;
  }
  case test::common::buffer::Action::kStartsWith: {
    const std::string data = target_buffer.toString();
    FUZZ_ASSERT(target_buffer.startsWith(action.starts_with()) ==
                (data.find(action.starts_with()) == 0));
    break;
  }
  default:
    // Maybe nothing is set?
    break;
  }

  return allocated;
}

} // namespace

void executeActions(const test::common::buffer::BufferFuzzTestCase& input, BufferList& buffers,
                    BufferList& linear_buffers, Context& ctxt) {
  // Soft bound on the available memory for allocation to avoid OOMs and
  // timeouts.
  uint32_t available_alloc = 2 * MaxAllocation;
  constexpr auto max_actions = 128;
  for (int i = 0; i < std::min(max_actions, input.actions().size()); ++i) {
    const char insert_value = 'a' + i % 26;
    const auto& action = input.actions(i);
    ENVOY_LOG_MISC(debug, "Action {}", action.DebugString());
    const uint32_t allocated = bufferAction(ctxt, insert_value, available_alloc, buffers, action);
    const uint32_t linear_allocated =
        bufferAction(ctxt, insert_value, available_alloc, linear_buffers, action);
    FUZZ_ASSERT(allocated == linear_allocated);
    FUZZ_ASSERT(allocated <= available_alloc);
    available_alloc -= allocated;
    // When tracing, dump everything.
    for (uint32_t j = 0; j < BufferCount; ++j) {
      ENVOY_LOG_MISC(trace, "Buffer at index {}", j);
      ENVOY_LOG_MISC(trace, "B: {}", buffers[j]->toString());
      ENVOY_LOG_MISC(trace, "L: {}", linear_buffers[j]->toString());
    }
    // Verification pass, only non-mutating methods for buffers.
    uint64_t current_allocated_bytes = 0;
    for (uint32_t j = 0; j < BufferCount; ++j) {
      // As an optimization, since we know that StringBuffer is just going to
      // return the pointer to its std::string array, we can avoid the
      // toString() copy here.
      const uint64_t linear_buffer_length = linear_buffers[j]->length();
      if (buffers[j]->toString() !=
          absl::string_view(
              static_cast<const char*>(linear_buffers[j]->linearize(linear_buffer_length)),
              linear_buffer_length)) {
        ENVOY_LOG_MISC(debug, "Mismatched buffers at index {}", j);
        ENVOY_LOG_MISC(debug, "B: {}", buffers[j]->toString());
        ENVOY_LOG_MISC(debug, "L: {}", linear_buffers[j]->toString());
        FUZZ_ASSERT(false);
      }
      FUZZ_ASSERT(buffers[j]->length() == linear_buffer_length);
      current_allocated_bytes += linear_buffer_length;
    }
    ENVOY_LOG_MISC(debug, "[{} MB allocated total]", current_allocated_bytes / (1024.0 * 1024));
    // We bail out if buffers get too big, otherwise we will OOM the sanitizer.
    // We can't use Memory::Stats::totalCurrentlyAllocated() here as we don't
    // have tcmalloc in ASAN builds, so just do a simple count.
    if (current_allocated_bytes > TotalMaxAllocation) {
      ENVOY_LOG_MISC(debug, "Terminating early with total buffer length {} to avoid OOM",
                     current_allocated_bytes);
      break;
    }
  }
}

void BufferFuzz::bufferFuzz(const test::common::buffer::BufferFuzzTestCase& input) {
  Context ctxt;
  // Fuzzed buffers.
  BufferList buffers;
  // Shadow buffers based on StringBuffer.
  BufferList linear_buffers;
  for (uint32_t i = 0; i < BufferCount; ++i) {
    buffers.emplace_back(new Buffer::OwnedImpl());
    linear_buffers.emplace_back(new StringBuffer());
  }
  executeActions(input, buffers, linear_buffers, ctxt);
}

} // namespace Envoy
