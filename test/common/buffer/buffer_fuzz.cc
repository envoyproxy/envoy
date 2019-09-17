#include "test/common/buffer/buffer_fuzz.h"

#include <fcntl.h>
#include <unistd.h>

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/common/stack_array.h"
#include "common/memory/stats.h"
#include "common/network/io_socket_handle_impl.h"

#include "absl/strings/match.h"
#include "gtest/gtest.h"

// Strong assertion that applies across all compilation modes and doesn't rely
// on gtest, which only provides soft fails that don't trip oss-fuzz failures.
#define FUZZ_ASSERT(x) RELEASE_ASSERT(x, "")

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
  std::vector<std::unique_ptr<std::string>> strings_;
};

// Bound the maximum allocation size.
constexpr uint32_t MaxAllocation = 2 * 1024 * 1024;

uint32_t clampSize(uint32_t size, uint32_t max_alloc) {
  return std::min(size, std::min(MaxAllocation, max_alloc));
}

void releaseFragmentAllocation(const void* p, size_t, const Buffer::BufferFragmentImpl*) {
  ::free(const_cast<void*>(p));
}

// Really simple string implementation of Buffer.
class StringBuffer : public Buffer::Instance {
public:
  void add(const void* data, uint64_t size) override {
    data_ += std::string(std::string(static_cast<const char*>(data), size));
  }

  void addBufferFragment(Buffer::BufferFragment& fragment) override {
    add(fragment.data(), fragment.size());
    fragment.done();
  }

  void add(absl::string_view data) override { add(data.data(), data.size()); }

  void add(const Buffer::Instance& data) override {
    const StringBuffer& src = dynamic_cast<const StringBuffer&>(data);
    data_ += src.data_;
  }

  void prepend(absl::string_view data) override { data_ = std::string(data) + data_; }

  void prepend(Instance& data) override {
    StringBuffer& src = dynamic_cast<StringBuffer&>(data);
    data_ = src.data_ + data_;
    src.data_.clear();
  }

  void commit(Buffer::RawSlice* iovecs, uint64_t num_iovecs) override {
    FUZZ_ASSERT(num_iovecs == 1);
    FUZZ_ASSERT(tmp_buf_.get() == iovecs[0].mem_);
    data_ += std::string(tmp_buf_.get(), iovecs[0].len_);
  }

  void copyOut(size_t start, uint64_t size, void* data) const override {
    ::memcpy(data, data_.data() + start, size);
  }

  void drain(uint64_t size) override { data_ = data_.substr(size); }

  uint64_t getRawSlices(Buffer::RawSlice* out, uint64_t out_size) const override {
    FUZZ_ASSERT(out_size > 0);
    // Sketchy, but probably will work for test purposes.
    out->mem_ = const_cast<char*>(data_.data());
    out->len_ = data_.size();
    return 1;
  }

  uint64_t length() const override { return data_.size(); }

  void* linearize(uint32_t /*size*/) override {
    // Sketchy, but probably will work for test purposes.
    return const_cast<char*>(data_.data());
  }

  void move(Buffer::Instance& rhs) override { move(rhs, rhs.length()); }

  void move(Buffer::Instance& rhs, uint64_t length) override {
    StringBuffer& src = dynamic_cast<StringBuffer&>(rhs);
    data_ += src.data_.substr(0, length);
    src.data_ = src.data_.substr(length);
  }

  Api::IoCallUint64Result read(Network::IoHandle& io_handle, uint64_t max_length) override {
    FUZZ_ASSERT(max_length <= MaxAllocation);
    Buffer::RawSlice slice{tmp_buf_.get(), MaxAllocation};
    Api::IoCallUint64Result result = io_handle.readv(max_length, &slice, 1);
    FUZZ_ASSERT(result.ok() && result.rc_ > 0);
    data_ += std::string(tmp_buf_.get(), result.rc_);
    return result;
  }

  uint64_t reserve(uint64_t length, Buffer::RawSlice* iovecs, uint64_t num_iovecs) override {
    FUZZ_ASSERT(num_iovecs > 0);
    FUZZ_ASSERT(length <= MaxAllocation);
    iovecs[0].mem_ = tmp_buf_.get();
    iovecs[0].len_ = length;
    return 1;
  }

  ssize_t search(const void* data, uint64_t size, size_t start) const override {
    return data_.find(std::string(static_cast<const char*>(data), size), start);
  }

  bool startsWith(absl::string_view data) const override { return absl::StartsWith(data_, data); }

  std::string toString() const override { return data_; }

  Api::IoCallUint64Result write(Network::IoHandle& io_handle) override {
    const Buffer::RawSlice slice{const_cast<char*>(data_.data()), data_.size()};
    Api::IoCallUint64Result result = io_handle.writev(&slice, 1);
    FUZZ_ASSERT(result.ok());
    data_ = data_.substr(result.rc_);
    return result;
  }

  std::string data_;
  std::unique_ptr<char[]> tmp_buf_{new char[MaxAllocation]};
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
    const uint32_t previous_length = target_buffer.length();
    const std::string new_value{static_cast<char*>(p), size};
    target_buffer.addBufferFragment(*ctxt.fragments_.back());
    FUZZ_ASSERT(previous_length == target_buffer.search(new_value.data(), size, previous_length));
    break;
  }
  case test::common::buffer::Action::kAddString: {
    const uint32_t size = clampSize(action.add_string(), max_alloc);
    allocated += size;
    auto string = std::make_unique<std::string>(size, insert_value);
    ctxt.strings_.emplace_back(std::move(string));
    const uint32_t previous_length = target_buffer.length();
    target_buffer.add(absl::string_view(*ctxt.strings_.back()));
    FUZZ_ASSERT(previous_length ==
                target_buffer.search(ctxt.strings_.back()->data(), size, previous_length));
    break;
  }
  case test::common::buffer::Action::kAddBuffer: {
    const uint32_t source_index = action.add_buffer() % BufferCount;
    if (target_index == source_index) {
      break;
    }
    Buffer::Instance& source_buffer = *buffers[source_index];
    const std::string source_contents = source_buffer.toString();
    const uint32_t previous_length = target_buffer.length();
    target_buffer.add(source_buffer);
    FUZZ_ASSERT(previous_length == target_buffer.search(source_contents.data(),
                                                        source_contents.size(), previous_length));
    break;
  }
  case test::common::buffer::Action::kPrependString: {
    const uint32_t size = clampSize(action.prepend_string(), max_alloc);
    allocated += size;
    auto string = std::make_unique<std::string>(size, insert_value);
    ctxt.strings_.emplace_back(std::move(string));
    target_buffer.prepend(absl::string_view(*ctxt.strings_.back()));
    FUZZ_ASSERT(target_buffer.search(ctxt.strings_.back()->data(), size, 0) == 0);
    break;
  }
  case test::common::buffer::Action::kPrependBuffer: {
    const uint32_t source_index = action.prepend_buffer() % BufferCount;
    if (target_index == source_index) {
      break;
    }
    Buffer::Instance& source_buffer = *buffers[source_index];
    const std::string source_contents = source_buffer.toString();
    target_buffer.prepend(source_buffer);
    FUZZ_ASSERT(target_buffer.search(source_contents.data(), source_contents.size(), 0) == 0);
    break;
  }
  case test::common::buffer::Action::kReserveCommit: {
    const uint32_t previous_length = target_buffer.length();
    const uint32_t reserve_length = clampSize(action.reserve_commit().reserve_length(), max_alloc);
    allocated += reserve_length;
    if (reserve_length == 0) {
      break;
    }
    constexpr uint32_t reserve_slices = 16;
    Buffer::RawSlice slices[reserve_slices];
    const uint32_t allocated_slices = target_buffer.reserve(reserve_length, slices, reserve_slices);
    uint32_t allocated_length = 0;
    for (uint32_t i = 0; i < allocated_slices; ++i) {
      ::memset(slices[i].mem_, insert_value, slices[i].len_);
      allocated_length += slices[i].len_;
    }
    FUZZ_ASSERT(reserve_length <= allocated_length);
    const uint32_t target_length =
        std::min(reserve_length, action.reserve_commit().commit_length());
    uint32_t shrink_length = allocated_length;
    int32_t shrink_slice = allocated_slices - 1;
    while (shrink_length > target_length) {
      FUZZ_ASSERT(shrink_slice >= 0);
      const uint32_t available = slices[shrink_slice].len_;
      const uint32_t remainder = shrink_length - target_length;
      if (available >= remainder) {
        slices[shrink_slice].len_ -= remainder;
        break;
      }
      shrink_length -= available;
      slices[shrink_slice--].len_ = 0;
    }
    target_buffer.commit(slices, allocated_slices);
    FUZZ_ASSERT(previous_length + target_length == target_buffer.length());
    break;
  }
  case test::common::buffer::Action::kCopyOut: {
    const uint32_t start =
        std::min(action.copy_out().start(), static_cast<uint32_t>(target_buffer.length()));
    uint8_t copy_buffer[2 * 1024 * 1024];
    const uint32_t length =
        std::min(static_cast<uint32_t>(target_buffer.length() - start),
                 std::min(action.copy_out().length(), static_cast<uint32_t>(sizeof(copy_buffer))));
    target_buffer.copyOut(start, length, copy_buffer);
    const std::string contents = target_buffer.toString();
    FUZZ_ASSERT(::memcmp(copy_buffer, contents.data() + start, length) == 0);
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
    Buffer::RawSlice slices[1];
    const uint64_t slices_used = target_buffer.getRawSlices(slices, 1);
    if (linearize_size > 0) {
      FUZZ_ASSERT(slices_used >= 1);
      FUZZ_ASSERT(slices[0].len_ >= linearize_size);
    }
    break;
  }
  case test::common::buffer::Action::kMove: {
    const uint32_t source_index = action.move().source_index() % BufferCount;
    if (target_index == source_index) {
      break;
    }
    Buffer::Instance& source_buffer = *buffers[source_index];
    if (action.move().length() == 0) {
      target_buffer.move(source_buffer);
    } else {
      target_buffer.move(source_buffer, std::min(static_cast<uint32_t>(source_buffer.length()),
                                                 action.move().length()));
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
    const uint32_t previous_length = target_buffer.length();
    Api::IoCallUint64Result result = target_buffer.read(io_handle, max_length);
    FUZZ_ASSERT(result.rc_ == static_cast<uint64_t>(rc));
    FUZZ_ASSERT(::close(pipe_fds[1]) == 0);
    FUZZ_ASSERT(previous_length == target_buffer.search(data.data(), rc, previous_length));
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
      const auto result = target_buffer.write(io_handle);
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
  default:
    // Maybe nothing is set?
    break;
  }

  return allocated;
}

} // namespace

void BufferFuzz::bufferFuzz(const test::common::buffer::BufferFuzzTestCase& input, bool old_impl) {
  ENVOY_LOG_MISC(trace, "Using {} buffer implementation", old_impl ? "old" : "new");
  Buffer::OwnedImpl::useOldImpl(old_impl);
  Context ctxt;
  // Fuzzed buffers.
  BufferList buffers;
  // Shadow buffers based on StringBuffer.
  BufferList linear_buffers;
  for (uint32_t i = 0; i < BufferCount; ++i) {
    buffers.emplace_back(new Buffer::OwnedImpl());
    linear_buffers.emplace_back(new StringBuffer());
  }

  const uint64_t initial_allocated_bytes = Memory::Stats::totalCurrentlyAllocated();

  // Soft bound on the available memory for allocation to avoid OOMs and
  // timeouts.
  uint32_t available_alloc = 2 * MaxAllocation;
  constexpr auto max_actions = 1024;
  for (int i = 0; i < std::min(max_actions, input.actions().size()); ++i) {
    const char insert_value = 'a' + i % 26;
    const auto& action = input.actions(i);
    const uint64_t current_allocated_bytes = Memory::Stats::totalCurrentlyAllocated();
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
    for (uint32_t j = 0; j < BufferCount; ++j) {
      if (buffers[j]->toString() != linear_buffers[j]->toString()) {
        ENVOY_LOG_MISC(debug, "Mismatched buffers at index {}", j);
        ENVOY_LOG_MISC(debug, "B: {}", buffers[j]->toString());
        ENVOY_LOG_MISC(debug, "L: {}", linear_buffers[j]->toString());
        FUZZ_ASSERT(false);
      }
      FUZZ_ASSERT(buffers[j]->length() == linear_buffers[j]->length());
      constexpr uint32_t max_slices = 16;
      Buffer::RawSlice slices[max_slices];
      buffers[j]->getRawSlices(slices, max_slices);
      // This string should never appear (e.g. we don't synthesize _garbage as a
      // pattern), verify that it's never found.
      std::string garbage{"_garbage"};
      FUZZ_ASSERT(buffers[j]->search(garbage.data(), garbage.size(), 0) == -1);
    }
    ENVOY_LOG_MISC(debug, "[{} MB allocated total, {} MB since start]",
                   current_allocated_bytes / (1024.0 * 1024),
                   (current_allocated_bytes - initial_allocated_bytes) / (1024.0 * 1024));
    // We bail out if buffers get too big, otherwise we will OOM the sanitizer.
    // We can't use Memory::Stats::totalCurrentlyAllocated() here as we don't
    // have tcmalloc in ASAN builds, so just do a simple count.
    uint64_t total_length = 0;
    for (const auto& buf : buffers) {
      total_length += buf->length();
    }
    if (total_length > 4 * MaxAllocation) {
      ENVOY_LOG_MISC(debug, "Terminating early with total buffer length {} to avoid OOM",
                     total_length);
      break;
    }
  }
}

} // namespace Envoy
