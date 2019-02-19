#include <fcntl.h>
#include <unistd.h>

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/logger.h"

#include "test/common/buffer/buffer_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"

#include "gtest/gtest.h"

namespace Envoy {

namespace {

// The number of buffers tracked. Each buffer fuzzer action references one or
// more of these. We don't need a ton of buffers to capture the range of
// possible behaviors, let's assume only 3 for now.
constexpr uint32_t BufferCount = 3;

// These data are exogenous to the buffer, we don't need to worry about their
// deallocation, just keep them around until the fuzz run is over.
struct Context {
  std::vector<std::unique_ptr<Buffer::BufferFragmentImpl>> fragments_;
  std::vector<std::unique_ptr<std::string>> strings_;
};

// Bound the maximum allocation size.
constexpr uint32_t MaxAllocation = 16 * 1024 * 1024;

uint32_t clampSize(uint32_t size) { return std::min(size, MaxAllocation); }

void releaseFragmentAllocation(const void* p, size_t, const Buffer::BufferFragmentImpl*) {
  ::free(const_cast<void*>(p));
}

// Process a single buffer operation.
void bufferAction(Context& ctxt, Buffer::OwnedImpl buffers[],
                  const test::common::buffer::Action& action) {
  const uint32_t target_index = action.target_index() % BufferCount;
  Buffer::OwnedImpl& target_buffer = buffers[target_index];

  switch (action.action_selector_case()) {
  case test::common::buffer::Action::kAddBufferFragment: {
    const uint32_t size = clampSize(action.add_buffer_fragment());
    void* p = ::malloc(size);
    ASSERT(p != nullptr);
    ::memset(p, 'b', size);
    auto fragment =
        std::make_unique<Buffer::BufferFragmentImpl>(p, size, releaseFragmentAllocation);
    ctxt.fragments_.emplace_back(std::move(fragment));
    const uint32_t previous_length = target_buffer.length();
    target_buffer.addBufferFragment(*ctxt.fragments_.back());
    ASSERT(previous_length == target_buffer.search(p, size, previous_length));
    break;
  }
  case test::common::buffer::Action::kAddString: {
    const uint32_t size = clampSize(action.add_string());
    auto string = std::make_unique<std::string>(size, 'a');
    ctxt.strings_.emplace_back(std::move(string));
    const uint32_t previous_length = target_buffer.length();
    target_buffer.add(absl::string_view(*ctxt.strings_.back()));
    ASSERT(previous_length ==
           target_buffer.search(ctxt.strings_.back()->data(), size, previous_length));
    break;
  }
  case test::common::buffer::Action::kAddBuffer: {
    const uint32_t source_index = action.add_buffer() % BufferCount;
    if (target_index == source_index) {
      break;
    }
    Buffer::OwnedImpl& source_buffer = buffers[source_index];
    const std::string source_contents = source_buffer.toString();
    const uint32_t previous_length = target_buffer.length();
    target_buffer.add(source_buffer);
    ASSERT(previous_length ==
           target_buffer.search(source_contents.data(), source_contents.size(), previous_length));
    break;
  }
  case test::common::buffer::Action::kPrependString: {
    const uint32_t size = clampSize(action.prepend_string());
    auto string = std::make_unique<std::string>(size, 'a');
    ctxt.strings_.emplace_back(std::move(string));
    target_buffer.prepend(absl::string_view(*ctxt.strings_.back()));
    ASSERT(target_buffer.search(ctxt.strings_.back()->data(), size, 0) == 0);
    break;
  }
  case test::common::buffer::Action::kPrependBuffer: {
    const uint32_t source_index = action.prepend_buffer() % BufferCount;
    if (target_index == source_index) {
      break;
    }
    Buffer::OwnedImpl& source_buffer = buffers[source_index];
    const std::string source_contents = source_buffer.toString();
    target_buffer.prepend(source_buffer);
    ASSERT(target_buffer.search(source_contents.data(), source_contents.size(), 0) == 0);
    break;
  }
  case test::common::buffer::Action::kReserveCommit: {
    const uint32_t previous_length = target_buffer.length();
    const uint32_t reserve_length = clampSize(action.reserve_commit().reserve_length());
    if (reserve_length == 0) {
      break;
    }
    constexpr uint32_t reserve_slices = 16;
    Buffer::RawSlice slices[reserve_slices];
    uint32_t allocated_slices = target_buffer.reserve(reserve_length, slices, reserve_slices);
    uint32_t allocated_length = 0;
    for (uint32_t i = 0; i < allocated_slices; ++i) {
      ::memset(slices[i].mem_, 'c', slices[i].len_);
      allocated_length += slices[i].len_;
    }
    const uint32_t target_length =
        std::min(allocated_length, action.reserve_commit().commit_length());
    uint32_t shrink_length = allocated_length;
    while (shrink_length > target_length) {
      ASSERT(allocated_slices > 0);
      const uint32_t available = slices[allocated_slices - 1].len_;
      const uint32_t remainder = shrink_length - target_length;
      if (available > remainder) {
        slices[allocated_slices - 1].len_ -= remainder;
        break;
      }
      shrink_length -= available;
      --allocated_slices;
    }
    target_buffer.commit(slices, allocated_slices);
    ASSERT(previous_length + target_length == target_buffer.length());
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
    ASSERT(::memcmp(copy_buffer, contents.data() + start, length) == 0);
    break;
  }
  case test::common::buffer::Action::kDrain: {
    const uint32_t previous_length = target_buffer.length();
    const uint32_t drain_length =
        std::min(static_cast<uint32_t>(target_buffer.length()), action.drain());
    target_buffer.drain(drain_length);
    ASSERT(previous_length - drain_length == target_buffer.length());
    break;
  }
  case test::common::buffer::Action::kLinearize: {
    const uint32_t linearize_size =
        std::min(static_cast<uint32_t>(target_buffer.length()), action.linearize());
    target_buffer.linearize(linearize_size);
    Buffer::RawSlice slices[1];
    const uint64_t slices_used = target_buffer.getRawSlices(slices, 1);
    if (linearize_size > 0) {
      ASSERT(slices_used == 1);
      ASSERT(slices[0].len_ >= linearize_size);
    }
    break;
  }
  case test::common::buffer::Action::kMove: {
    const uint32_t source_index = action.move().source_index() % BufferCount;
    if (target_index == source_index) {
      break;
    }
    Buffer::OwnedImpl& source_buffer = buffers[source_index];
    if (action.move().length() == 0) {
      target_buffer.move(source_buffer);
    } else {
      target_buffer.move(source_buffer, std::min(static_cast<uint32_t>(source_buffer.length()),
                                                 action.move().length()));
    }
    break;
  }
  case test::common::buffer::Action::kRead: {
    const uint32_t max_length = clampSize(action.read());
    if (max_length == 0) {
      break;
    }
    int pipe_fds[2] = {0, 0};
    ASSERT(::pipe(pipe_fds) == 0);
    ASSERT(::fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK) == 0);
    ASSERT(::fcntl(pipe_fds[1], F_SETFL, O_NONBLOCK) == 0);
    std::string data(max_length, 'd');
    const int rc = ::write(pipe_fds[1], data.data(), max_length);
    ASSERT(rc > 0);
    ASSERT(target_buffer.read(pipe_fds[0], max_length).rc_ == rc);
    ASSERT(::close(pipe_fds[0]) == 0);
    ASSERT(::close(pipe_fds[1]) == 0);
    break;
  }
  case test::common::buffer::Action::kWrite: {
    int pipe_fds[2] = {0, 0};
    ASSERT(::pipe(pipe_fds) == 0);
    ASSERT(::fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK) == 0);
    ASSERT(::fcntl(pipe_fds[1], F_SETFL, O_NONBLOCK) == 0);
    const bool empty = target_buffer.length() == 0;
    const int rc = target_buffer.write(pipe_fds[1]).rc_;
    ASSERT(empty ? rc == 0 : rc > 0);
    ASSERT(::close(pipe_fds[0]) == 0);
    ASSERT(::close(pipe_fds[1]) == 0);
    break;
  }
  default:
    // Maybe nothing is set?
    break;
  }
}

} // namespace

// Fuzz the owned buffer implementation.
DEFINE_PROTO_FUZZER(const test::common::buffer::BufferFuzzTestCase& input) {
  Context ctxt;
  // Fuzzed buffers.
  Buffer::OwnedImpl buffers[BufferCount];
  // Shadows of buffers that are linearized at the end of each operation. This
  // is an approximation of a simple buffer implementation without buffer
  // slices.
  Buffer::OwnedImpl linear_buffers[BufferCount];

  for (int i = 0; i < input.actions().size(); ++i) {
    const auto& action = input.actions(i);
    ENVOY_LOG_MISC(debug, "Action {}", action.DebugString());
    bufferAction(ctxt, buffers, action);
    bufferAction(ctxt, linear_buffers, action);
    // Verification pass, only non-mutating methods for buffers.
    for (uint32_t j = 0; j < BufferCount; ++j) {
      // Linearize shadow buffer, compare equal toString() and size().
      linear_buffers[j].linearize(linear_buffers[j].length());
      if (buffers[j].toString() != linear_buffers[j].toString()) {
        ENVOY_LOG_MISC(debug, "Mismatched buffers at index {}", j);
        ENVOY_LOG_MISC(debug, "B: {}", buffers[j].toString());
        ENVOY_LOG_MISC(debug, "L: {}", linear_buffers[j].toString());
        ASSERT(false);
      }
      ASSERT(buffers[j].length() == linear_buffers[j].length());
      constexpr uint32_t max_slices = 16;
      Buffer::RawSlice slices[max_slices];
      buffers[j].getRawSlices(slices, max_slices);
      std::string garbage{"_garbage"};
      ASSERT(buffers[j].search(garbage.data(), garbage.size(), 0) == -1);
    }
  }
}

} // namespace Envoy
