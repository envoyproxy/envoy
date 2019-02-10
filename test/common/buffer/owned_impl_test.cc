#include "common/api/os_sys_calls_impl.h"
#include "common/buffer/buffer_impl.h"

#include "test/common/buffer/utility.h"
#include "test/mocks/api/mocks.h"
#include "test/test_common/test_base.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Buffer {
namespace {

class OwnedImplTest : public BufferImplementationParamTest {
public:
  bool release_callback_called_ = false;

protected:
  static void clearReservation(Buffer::RawSlice* iovecs, uint64_t num_iovecs, OwnedImpl& buffer) {
    for (uint64_t i = 0; i < num_iovecs; i++) {
      iovecs[i].len_ = 0;
    }
    buffer.commit(iovecs, num_iovecs);
  }

  static void commitReservation(Buffer::RawSlice* iovecs, uint64_t num_iovecs, OwnedImpl& buffer) {
    buffer.commit(iovecs, num_iovecs);
  }
};

INSTANTIATE_TEST_CASE_P(OwnedImplTest, OwnedImplTest,
                        testing::ValuesIn({BufferImplementation::Old, BufferImplementation::New}));

TEST_P(OwnedImplTest, AddBufferFragmentNoCleanup) {
  char input[] = "hello world";
  BufferFragmentImpl frag(input, 11, nullptr);
  Buffer::OwnedImpl buffer;
  verifyImplementation(buffer);
  buffer.addBufferFragment(frag);
  EXPECT_EQ(11, buffer.length());

  buffer.drain(11);
  EXPECT_EQ(0, buffer.length());
}

TEST_P(OwnedImplTest, AddBufferFragmentWithCleanup) {
  char input[] = "hello world";
  BufferFragmentImpl frag(input, 11, [this](const void*, size_t, const BufferFragmentImpl*) {
    release_callback_called_ = true;
  });
  Buffer::OwnedImpl buffer;
  verifyImplementation(buffer);
  buffer.addBufferFragment(frag);
  EXPECT_EQ(11, buffer.length());

  buffer.drain(5);
  EXPECT_EQ(6, buffer.length());
  EXPECT_FALSE(release_callback_called_);

  buffer.drain(6);
  EXPECT_EQ(0, buffer.length());
  EXPECT_TRUE(release_callback_called_);
}

TEST_P(OwnedImplTest, AddBufferFragmentDynamicAllocation) {
  char input_stack[] = "hello world";
  char* input = new char[11];
  std::copy(input_stack, input_stack + 11, input);

  BufferFragmentImpl* frag = new BufferFragmentImpl(
      input, 11, [this](const void* data, size_t, const BufferFragmentImpl* frag) {
        release_callback_called_ = true;
        delete[] static_cast<const char*>(data);
        delete frag;
      });

  Buffer::OwnedImpl buffer;
  verifyImplementation(buffer);
  buffer.addBufferFragment(*frag);
  EXPECT_EQ(11, buffer.length());

  buffer.drain(5);
  EXPECT_EQ(6, buffer.length());
  EXPECT_FALSE(release_callback_called_);

  buffer.drain(6);
  EXPECT_EQ(0, buffer.length());
  EXPECT_TRUE(release_callback_called_);
}

TEST_P(OwnedImplTest, Add) {
  const std::string string1 = "Hello, ", string2 = "World!";
  Buffer::OwnedImpl buffer;
  verifyImplementation(buffer);

  buffer.add(string1);
  EXPECT_EQ(string1.size(), buffer.length());
  EXPECT_EQ(string1, buffer.toString());

  buffer.add(string2);
  EXPECT_EQ(string1.size() + string2.size(), buffer.length());
  EXPECT_EQ(string1 + string2, buffer.toString());

  // Append a large string that will only partially fit in the space remaining
  // at the end of the buffer.
  std::string big_suffix;
  big_suffix.reserve(16385);
  for (unsigned i = 0; i < 16; i++) {
    big_suffix += std::string(1024, 'A' + i);
  }
  big_suffix.push_back('-');
  buffer.add(big_suffix);
  EXPECT_EQ(string1.size() + string2.size() + big_suffix.size(), buffer.length());
  EXPECT_EQ(string1 + string2 + big_suffix, buffer.toString());
}

TEST_P(OwnedImplTest, Prepend) {
  const std::string suffix = "World!", prefix = "Hello, ";
  Buffer::OwnedImpl buffer;
  verifyImplementation(buffer);
  buffer.add(suffix);
  buffer.prepend(prefix);

  EXPECT_EQ(suffix.size() + prefix.size(), buffer.length());
  EXPECT_EQ(prefix + suffix, buffer.toString());

  // Prepend a large string that will only partially fit in the space remaining
  // at the front of the buffer.
  std::string big_prefix;
  big_prefix.reserve(16385);
  for (unsigned i = 0; i < 16; i++) {
    big_prefix += std::string(1024, 'A' + i);
  }
  big_prefix.push_back('-');
  buffer.prepend(big_prefix);
  EXPECT_EQ(big_prefix.size() + prefix.size() + suffix.size(), buffer.length());
  EXPECT_EQ(big_prefix + prefix + suffix, buffer.toString());
}

TEST_P(OwnedImplTest, PrependToEmptyBuffer) {
  std::string data = "Hello, World!";
  Buffer::OwnedImpl buffer;
  verifyImplementation(buffer);
  buffer.prepend(data);

  EXPECT_EQ(data.size(), buffer.length());
  EXPECT_EQ(data, buffer.toString());

  buffer.prepend("");

  EXPECT_EQ(data.size(), buffer.length());
  EXPECT_EQ(data, buffer.toString());
}

TEST_P(OwnedImplTest, PrependBuffer) {
  std::string suffix = "World!", prefix = "Hello, ";
  Buffer::OwnedImpl buffer;
  verifyImplementation(buffer);
  buffer.add(suffix);
  Buffer::OwnedImpl prefixBuffer;
  verifyImplementation(buffer);
  prefixBuffer.add(prefix);

  buffer.prepend(prefixBuffer);

  EXPECT_EQ(suffix.size() + prefix.size(), buffer.length());
  EXPECT_EQ(prefix + suffix, buffer.toString());
  EXPECT_EQ(0, prefixBuffer.length());
}

TEST_P(OwnedImplTest, Write) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Buffer::OwnedImpl buffer;
  verifyImplementation(buffer);
  buffer.add("example");
  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{7, 0}));
  Api::SysCallIntResult result = buffer.write(-1);
  EXPECT_EQ(7, result.rc_);
  EXPECT_EQ(0, buffer.length());

  buffer.add("example");
  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{6, 0}));
  result = buffer.write(-1);
  EXPECT_EQ(6, result.rc_);
  EXPECT_EQ(1, buffer.length());

  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{0, 0}));
  result = buffer.write(-1);
  EXPECT_EQ(0, result.rc_);
  EXPECT_EQ(1, buffer.length());

  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{-1, 0}));
  result = buffer.write(-1);
  EXPECT_EQ(-1, result.rc_);
  EXPECT_EQ(1, buffer.length());

  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{1, 0}));
  result = buffer.write(-1);
  EXPECT_EQ(1, result.rc_);
  EXPECT_EQ(0, buffer.length());

  EXPECT_CALL(os_sys_calls, writev(_, _, _)).Times(0);
  result = buffer.write(-1);
  EXPECT_EQ(0, result.rc_);
  EXPECT_EQ(0, buffer.length());
}

TEST_P(OwnedImplTest, Read) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Buffer::OwnedImpl buffer;
  verifyImplementation(buffer);
  EXPECT_CALL(os_sys_calls, readv(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{0, 0}));
  Api::SysCallIntResult result = buffer.read(-1, 100);
  EXPECT_EQ(0, result.rc_);
  EXPECT_EQ(0, buffer.length());

  EXPECT_CALL(os_sys_calls, readv(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{-1, 0}));
  result = buffer.read(-1, 100);
  EXPECT_EQ(-1, result.rc_);
  EXPECT_EQ(0, buffer.length());

  EXPECT_CALL(os_sys_calls, readv(_, _, _)).Times(0);
  result = buffer.read(-1, 0);
  EXPECT_EQ(0, result.rc_);
  EXPECT_EQ(0, buffer.length());
}

TEST_P(OwnedImplTest, ReserveCommit) {
  // This fragment will later be added to the buffer. It is declared in an enclosing scope to
  // ensure it is not destructed until after the buffer is.
  const std::string input = "Hello, world";
  BufferFragmentImpl fragment(input.c_str(), input.size(), nullptr);

  {
    Buffer::OwnedImpl buffer;
    verifyImplementation(buffer);

    // A zero-byte reservation should fail.
    static constexpr uint64_t NumIovecs = 16;
    Buffer::RawSlice iovecs[NumIovecs];
    uint64_t num_reserved = buffer.reserve(0, iovecs, NumIovecs);
    EXPECT_EQ(0, num_reserved);
    clearReservation(iovecs, num_reserved, buffer);
    EXPECT_EQ(0, buffer.length());

    // Test and commit a small reservation. This should succeed.
    num_reserved = buffer.reserve(1, iovecs, NumIovecs);
    EXPECT_EQ(1, num_reserved);
    // The implementation might provide a bigger reservation than requested.
    EXPECT_LE(1, iovecs[0].len_);
    iovecs[0].len_ = 1;
    commitReservation(iovecs, num_reserved, buffer);
    EXPECT_EQ(1, buffer.length());

    // The remaining tests validate internal optimizations of the new deque-of-slices
    // implementation, so they're not valid for the old evbuffer implementation.
    if (buffer.usesOldImpl()) {
      return;
    }

    // Request a reservation that fits in the remaining space at the end of the last slice.
    num_reserved = buffer.reserve(1, iovecs, NumIovecs);
    EXPECT_EQ(1, num_reserved);
    EXPECT_LE(1, iovecs[0].len_);
    iovecs[0].len_ = 1;
    const void* slice1 = iovecs[0].mem_;
    clearReservation(iovecs, num_reserved, buffer);

    // Request a reservation that is too large to fit in the remaining space at the end of
    // the last slice, and allow the buffer to use only one slice. This should result in the
    // creation of a new slice within the buffer.
    num_reserved = buffer.reserve(4096 - sizeof(OwnedSlice), iovecs, 1);
    const void* slice2 = iovecs[0].mem_;
    EXPECT_EQ(1, num_reserved);
    EXPECT_NE(slice1, slice2);
    clearReservation(iovecs, num_reserved, buffer);

    // Request the same size reservation, but allow the buffer to use multiple slices. This
    // should result in the buffer splitting the reservation between its last two slices.
    num_reserved = buffer.reserve(4096 - sizeof(OwnedSlice), iovecs, NumIovecs);
    EXPECT_EQ(2, num_reserved);
    EXPECT_EQ(slice1, iovecs[0].mem_);
    EXPECT_EQ(slice2, iovecs[1].mem_);
    clearReservation(iovecs, num_reserved, buffer);

    // Request a reservation that too big to fit in the existing slices. This should result
    // in the creation of a third slice.
    num_reserved = buffer.reserve(8192, iovecs, NumIovecs);
    EXPECT_EQ(3, num_reserved);
    EXPECT_EQ(slice1, iovecs[0].mem_);
    EXPECT_EQ(slice2, iovecs[1].mem_);
    const void* slice3 = iovecs[2].mem_;
    clearReservation(iovecs, num_reserved, buffer);

    // Append a fragment to the buffer, and then request a small reservation. The buffer
    // should make a new slice to satisfy the reservation; it cannot safely use any of
    // the previously seen slices, because they are no longer at the end of the buffer.
    buffer.addBufferFragment(fragment);
    EXPECT_EQ(13, buffer.length());
    num_reserved = buffer.reserve(1, iovecs, NumIovecs);
    EXPECT_EQ(1, num_reserved);
    EXPECT_NE(slice1, iovecs[0].mem_);
    EXPECT_NE(slice2, iovecs[0].mem_);
    EXPECT_NE(slice3, iovecs[0].mem_);
    commitReservation(iovecs, num_reserved, buffer);
    EXPECT_EQ(14, buffer.length());
  }
}

TEST_P(OwnedImplTest, Search) {
  // Populate a buffer with a string split across many small slices, to
  // exercise edge cases in the search implementation.
  static const char* Inputs[] = {"ab", "a", "", "aaa", "b", "a", "aaa", "ab", "a"};
  Buffer::OwnedImpl buffer;
  verifyImplementation(buffer);
  for (const auto& input : Inputs) {
    buffer.appendSliceForTest(input);
  }
  EXPECT_STREQ("abaaaabaaaaaba", buffer.toString().c_str());

  EXPECT_EQ(-1, buffer.search("c", 1, 0));
  EXPECT_EQ(0, buffer.search("", 0, 0));
  EXPECT_EQ(0, buffer.search("a", 1, 0));
  EXPECT_EQ(1, buffer.search("b", 1, 1));
  EXPECT_EQ(2, buffer.search("a", 1, 1));
  EXPECT_EQ(0, buffer.search("abaa", 4, 0));
  EXPECT_EQ(2, buffer.search("aaaa", 4, 0));
  EXPECT_EQ(2, buffer.search("aaaa", 4, 1));
  EXPECT_EQ(2, buffer.search("aaaa", 4, 2));
  EXPECT_EQ(7, buffer.search("aaaaab", 6, 0));
  EXPECT_EQ(0, buffer.search("abaaaabaaaaaba", 14, 0));
  EXPECT_EQ(12, buffer.search("ba", 2, 10));
  EXPECT_EQ(-1, buffer.search("abaaaabaaaaabaa", 15, 0));
}

TEST_P(OwnedImplTest, ToString) {
  Buffer::OwnedImpl buffer;
  verifyImplementation(buffer);
  EXPECT_EQ("", buffer.toString());
  auto append = [&buffer](absl::string_view str) { buffer.add(str.data(), str.size()); };
  append("Hello, ");
  EXPECT_EQ("Hello, ", buffer.toString());
  append("world!");
  EXPECT_EQ("Hello, world!", buffer.toString());

  // From debug inspection, I find that a second fragment is created at >1000 bytes.
  std::string long_string(5000, 'A');
  append(long_string);
  EXPECT_EQ(absl::StrCat("Hello, world!" + long_string), buffer.toString());
}

TEST_P(OwnedImplTest, AppendSliceForTest) {
  static constexpr size_t NumInputs = 3;
  static constexpr const char* Inputs[] = {"one", "2",
                                           ""
                                           "four",
                                           ""};
  Buffer::OwnedImpl buffer;
  RawSlice slices[NumInputs];
  EXPECT_EQ(0, buffer.getRawSlices(slices, NumInputs));
  for (const auto& input : Inputs) {
    buffer.appendSliceForTest(input);
  }
  // getRawSlices will only return the 3 slices with nonzero length.
  EXPECT_EQ(3, buffer.getRawSlices(slices, NumInputs));

  auto expectSlice = [](const RawSlice& slice, const char* expected) {
    size_t length = strlen(expected);
    EXPECT_EQ(length, slice.len_);
    EXPECT_EQ(0, memcmp(slice.mem_, expected, length));
  };

  expectSlice(slices[0], "one");
  expectSlice(slices[1], "2");
  expectSlice(slices[2], "four");
}

} // namespace
} // namespace Buffer
} // namespace Envoy
