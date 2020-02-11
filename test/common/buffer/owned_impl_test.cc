#include "envoy/api/io_error.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/io_socket_handle_impl.h"

#include "test/common/buffer/utility.h"
#include "test/mocks/api/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ContainerEq;
using testing::Return;

namespace Envoy {
namespace Buffer {
namespace {

class OwnedImplTest : public testing::Test {
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

  static void expectSlices(std::vector<std::vector<int>> buffer_list, OwnedImpl& buffer) {
    const auto& buffer_slices = buffer.describeSlicesForTest();
    for (uint64_t i = 0; i < buffer_slices.size(); i++) {
      EXPECT_EQ(buffer_slices[i].data, buffer_list[i][0]);
      EXPECT_EQ(buffer_slices[i].reservable, buffer_list[i][1]);
      EXPECT_EQ(buffer_slices[i].capacity, buffer_list[i][2]);
    }
  }
};

TEST_F(OwnedImplTest, AddBufferFragmentNoCleanup) {
  char input[] = "hello world";
  BufferFragmentImpl frag(input, 11, nullptr);
  Buffer::OwnedImpl buffer;
  buffer.addBufferFragment(frag);
  EXPECT_EQ(11, buffer.length());

  buffer.drain(11);
  EXPECT_EQ(0, buffer.length());
}

TEST_F(OwnedImplTest, AddBufferFragmentWithCleanup) {
  std::string input(2048, 'a');
  BufferFragmentImpl frag(
      input.c_str(), input.size(),
      [this](const void*, size_t, const BufferFragmentImpl*) { release_callback_called_ = true; });
  Buffer::OwnedImpl buffer;
  buffer.addBufferFragment(frag);
  EXPECT_EQ(2048, buffer.length());

  buffer.drain(2000);
  EXPECT_EQ(48, buffer.length());
  EXPECT_FALSE(release_callback_called_);

  buffer.drain(48);
  EXPECT_EQ(0, buffer.length());
  EXPECT_TRUE(release_callback_called_);
}

TEST_F(OwnedImplTest, AddEmptyFragment) {
  char input[] = "hello world";
  BufferFragmentImpl frag1(input, 11, [](const void*, size_t, const BufferFragmentImpl*) {});
  BufferFragmentImpl frag2("", 0, [this](const void*, size_t, const BufferFragmentImpl*) {
    release_callback_called_ = true;
  });
  Buffer::OwnedImpl buffer;
  buffer.addBufferFragment(frag1);
  EXPECT_EQ(11, buffer.length());

  buffer.addBufferFragment(frag2);
  EXPECT_EQ(11, buffer.length());

  buffer.drain(11);
  EXPECT_EQ(0, buffer.length());
  EXPECT_TRUE(release_callback_called_);
}

TEST_F(OwnedImplTest, AddBufferFragmentDynamicAllocation) {
  std::string input_str(2048, 'a');
  char* input = new char[2048];
  std::copy(input_str.c_str(), input_str.c_str() + 11, input);

  BufferFragmentImpl* frag = new BufferFragmentImpl(
      input, 2048, [this](const void* data, size_t, const BufferFragmentImpl* frag) {
        release_callback_called_ = true;
        delete[] static_cast<const char*>(data);
        delete frag;
      });

  Buffer::OwnedImpl buffer;
  buffer.addBufferFragment(*frag);
  EXPECT_EQ(2048, buffer.length());

  buffer.drain(2042);
  EXPECT_EQ(6, buffer.length());
  EXPECT_FALSE(release_callback_called_);

  buffer.drain(6);
  EXPECT_EQ(0, buffer.length());
  EXPECT_TRUE(release_callback_called_);
}

TEST_F(OwnedImplTest, AddOwnedBufferFragmentWithCleanup) {
  std::string input(2048, 'a');
  const size_t expected_length = input.size();
  auto frag = OwnedBufferFragmentImpl::create(
      {input.c_str(), expected_length},
      [this](const OwnedBufferFragmentImpl*) { release_callback_called_ = true; });
  Buffer::OwnedImpl buffer;
  buffer.addBufferFragment(*frag);
  EXPECT_EQ(expected_length, buffer.length());

  const uint64_t partial_drain_size = 5;
  buffer.drain(partial_drain_size);
  EXPECT_EQ(expected_length - partial_drain_size, buffer.length());
  EXPECT_FALSE(release_callback_called_);

  buffer.drain(expected_length - partial_drain_size);
  EXPECT_EQ(0, buffer.length());
  EXPECT_TRUE(release_callback_called_);
}

// Verify that OwnedBufferFragment work correctly when input buffer is allocated on the heap.
TEST_F(OwnedImplTest, AddOwnedBufferFragmentDynamicAllocation) {
  std::string input_str(2048, 'a');
  const size_t expected_length = input_str.size();
  char* input = new char[expected_length];
  std::copy(input_str.c_str(), input_str.c_str() + expected_length, input);

  auto* frag = OwnedBufferFragmentImpl::create({input, expected_length},
                                               [this, input](const OwnedBufferFragmentImpl* frag) {
                                                 release_callback_called_ = true;
                                                 delete[] input;
                                                 delete frag;
                                               })
                   .release();

  Buffer::OwnedImpl buffer;
  buffer.addBufferFragment(*frag);
  EXPECT_EQ(expected_length, buffer.length());

  const uint64_t partial_drain_size = 5;
  buffer.drain(partial_drain_size);
  EXPECT_EQ(expected_length - partial_drain_size, buffer.length());
  EXPECT_FALSE(release_callback_called_);

  buffer.drain(expected_length - partial_drain_size);
  EXPECT_EQ(0, buffer.length());
  EXPECT_TRUE(release_callback_called_);
}

TEST_F(OwnedImplTest, Add) {
  const std::string string1 = "Hello, ", string2 = "World!";
  Buffer::OwnedImpl buffer;
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

TEST_F(OwnedImplTest, Prepend) {
  const std::string suffix = "World!", prefix = "Hello, ";
  Buffer::OwnedImpl buffer;
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

TEST_F(OwnedImplTest, PrependToEmptyBuffer) {
  std::string data = "Hello, World!";
  Buffer::OwnedImpl buffer;
  buffer.prepend(data);

  EXPECT_EQ(data.size(), buffer.length());
  EXPECT_EQ(data, buffer.toString());

  buffer.prepend("");

  EXPECT_EQ(data.size(), buffer.length());
  EXPECT_EQ(data, buffer.toString());
}

TEST_F(OwnedImplTest, PrependBuffer) {
  std::string suffix = "World!", prefix = "Hello, ";
  Buffer::OwnedImpl buffer;
  buffer.add(suffix);
  Buffer::OwnedImpl prefixBuffer;
  prefixBuffer.add(prefix);

  buffer.prepend(prefixBuffer);

  EXPECT_EQ(suffix.size() + prefix.size(), buffer.length());
  EXPECT_EQ(prefix + suffix, buffer.toString());
  EXPECT_EQ(0, prefixBuffer.length());
}

TEST_F(OwnedImplTest, Write) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Buffer::OwnedImpl buffer;
  Network::IoSocketHandleImpl io_handle;
  buffer.add("example");
  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{7, 0}));
  Api::IoCallUint64Result result = buffer.write(io_handle);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(7, result.rc_);
  EXPECT_EQ(0, buffer.length());

  buffer.add("example");
  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{6, 0}));
  result = buffer.write(io_handle);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(6, result.rc_);
  EXPECT_EQ(1, buffer.length());

  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{0, 0}));
  result = buffer.write(io_handle);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(0, result.rc_);
  EXPECT_EQ(1, buffer.length());

  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{-1, 0}));
  result = buffer.write(io_handle);
  EXPECT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());
  EXPECT_EQ(0, result.rc_);
  EXPECT_EQ(1, buffer.length());

  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{-1, EAGAIN}));
  result = buffer.write(io_handle);
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());
  EXPECT_EQ(0, result.rc_);
  EXPECT_EQ(1, buffer.length());

  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{1, 0}));
  result = buffer.write(io_handle);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(1, result.rc_);
  EXPECT_EQ(0, buffer.length());

  EXPECT_CALL(os_sys_calls, writev(_, _, _)).Times(0);
  result = buffer.write(io_handle);
  EXPECT_EQ(0, result.rc_);
  EXPECT_EQ(0, buffer.length());
}

TEST_F(OwnedImplTest, Read) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Buffer::OwnedImpl buffer;
  Network::IoSocketHandleImpl io_handle;
  EXPECT_CALL(os_sys_calls, readv(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{0, 0}));
  Api::IoCallUint64Result result = buffer.read(io_handle, 100);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(0, result.rc_);
  EXPECT_EQ(0, buffer.length());
  EXPECT_THAT(buffer.describeSlicesForTest(), testing::IsEmpty());

  EXPECT_CALL(os_sys_calls, readv(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{-1, 0}));
  result = buffer.read(io_handle, 100);
  EXPECT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());
  EXPECT_EQ(0, result.rc_);
  EXPECT_EQ(0, buffer.length());
  EXPECT_THAT(buffer.describeSlicesForTest(), testing::IsEmpty());

  EXPECT_CALL(os_sys_calls, readv(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{-1, EAGAIN}));
  result = buffer.read(io_handle, 100);
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());
  EXPECT_EQ(0, result.rc_);
  EXPECT_EQ(0, buffer.length());
  EXPECT_THAT(buffer.describeSlicesForTest(), testing::IsEmpty());

  EXPECT_CALL(os_sys_calls, readv(_, _, _)).Times(0);
  result = buffer.read(io_handle, 0);
  EXPECT_EQ(0, result.rc_);
  EXPECT_EQ(0, buffer.length());
  EXPECT_THAT(buffer.describeSlicesForTest(), testing::IsEmpty());
}

TEST_F(OwnedImplTest, ReserveCommit) {
  // This fragment will later be added to the buffer. It is declared in an enclosing scope to
  // ensure it is not destructed until after the buffer is.
  const std::string input = "Hello, world";
  BufferFragmentImpl fragment(input.c_str(), input.size(), nullptr);

  {
    Buffer::OwnedImpl buffer;
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
    EXPECT_EQ(1, num_reserved);
    EXPECT_NE(slice1, iovecs[0].mem_);
    clearReservation(iovecs, num_reserved, buffer);

    // Request the same size reservation, but allow the buffer to use multiple slices. This
    // should result in the buffer creating a second slice and splitting the reservation between the
    // last two slices.
    num_reserved = buffer.reserve(4096 - sizeof(OwnedSlice), iovecs, NumIovecs);
    EXPECT_EQ(2, num_reserved);
    EXPECT_EQ(slice1, iovecs[0].mem_);
    clearReservation(iovecs, num_reserved, buffer);

    // Request a reservation that too big to fit in the existing slices. This should result
    // in the creation of a third slice.
    expectSlices({{1, 4055, 4056}}, buffer);
    buffer.reserve(4096 - sizeof(OwnedSlice), iovecs, NumIovecs);
    expectSlices({{1, 4055, 4056}, {0, 4056, 4056}}, buffer);
    const void* slice2 = iovecs[1].mem_;
    num_reserved = buffer.reserve(8192, iovecs, NumIovecs);
    expectSlices({{1, 4055, 4056}, {0, 4056, 4056}, {0, 4056, 4056}}, buffer);
    EXPECT_EQ(3, num_reserved);
    EXPECT_EQ(slice1, iovecs[0].mem_);
    EXPECT_EQ(slice2, iovecs[1].mem_);
    clearReservation(iovecs, num_reserved, buffer);

    // Append a fragment to the buffer, and then request a small reservation. The buffer
    // should make a new slice to satisfy the reservation; it cannot safely use any of
    // the previously seen slices, because they are no longer at the end of the buffer.
    expectSlices({{1, 4055, 4056}}, buffer);
    buffer.addBufferFragment(fragment);
    EXPECT_EQ(13, buffer.length());
    num_reserved = buffer.reserve(1, iovecs, NumIovecs);
    expectSlices({{1, 4055, 4056}, {12, 0, 12}, {0, 4056, 4056}}, buffer);
    EXPECT_EQ(1, num_reserved);
    EXPECT_NE(slice1, iovecs[0].mem_);
    commitReservation(iovecs, num_reserved, buffer);
    EXPECT_EQ(14, buffer.length());
  }
}

TEST_F(OwnedImplTest, ReserveCommitReuse) {
  Buffer::OwnedImpl buffer;
  static constexpr uint64_t NumIovecs = 2;
  Buffer::RawSlice iovecs[NumIovecs];

  // Reserve 8KB and commit all but a few bytes of it, to ensure that
  // the last slice of the buffer can hold part but not all of the
  // next reservation. Note that the buffer implementation might
  // allocate more than the requested 8KB. In case the implementation
  // uses a power-of-two allocator, the subsequent reservations all
  // request 16KB.
  uint64_t num_reserved = buffer.reserve(8192, iovecs, NumIovecs);
  EXPECT_EQ(1, num_reserved);
  iovecs[0].len_ = 8000;
  buffer.commit(iovecs, 1);
  EXPECT_EQ(8000, buffer.length());

  // Reserve 16KB. The resulting reservation should span 2 slices.
  // Commit part of the first slice and none of the second slice.
  num_reserved = buffer.reserve(16384, iovecs, NumIovecs);
  EXPECT_EQ(2, num_reserved);
  const void* first_slice = iovecs[0].mem_;
  iovecs[0].len_ = 1;
  expectSlices({{8000, 4248, 12248}, {0, 12248, 12248}}, buffer);
  buffer.commit(iovecs, 1);
  EXPECT_EQ(8001, buffer.length());
  EXPECT_EQ(first_slice, iovecs[0].mem_);
  // The second slice is now released because there's nothing in the second slice.
  expectSlices({{8001, 4247, 12248}}, buffer);

  // Reserve 16KB again.
  num_reserved = buffer.reserve(16384, iovecs, NumIovecs);
  expectSlices({{8001, 4247, 12248}, {0, 12248, 12248}}, buffer);
  EXPECT_EQ(2, num_reserved);
  EXPECT_EQ(static_cast<const uint8_t*>(first_slice) + 1,
            static_cast<const uint8_t*>(iovecs[0].mem_));
}

TEST_F(OwnedImplTest, ReserveReuse) {
  Buffer::OwnedImpl buffer;
  static constexpr uint64_t NumIovecs = 2;
  Buffer::RawSlice iovecs[NumIovecs];

  // Reserve some space and leave it uncommitted.
  uint64_t num_reserved = buffer.reserve(8192, iovecs, NumIovecs);
  EXPECT_EQ(1, num_reserved);
  const void* first_slice = iovecs[0].mem_;

  // Reserve more space and verify that it begins with the same slice from the last reservation.
  num_reserved = buffer.reserve(16384, iovecs, NumIovecs);
  EXPECT_EQ(2, num_reserved);
  EXPECT_EQ(first_slice, iovecs[0].mem_);
  const void* second_slice = iovecs[1].mem_;

  // Repeat the last reservation and verify that it yields the same slices.
  num_reserved = buffer.reserve(16384, iovecs, NumIovecs);
  EXPECT_EQ(2, num_reserved);
  EXPECT_EQ(first_slice, iovecs[0].mem_);
  EXPECT_EQ(second_slice, iovecs[1].mem_);
  expectSlices({{0, 12248, 12248}, {0, 8152, 8152}}, buffer);

  // Request a larger reservation, verify that the second entry is replaced with a block with a
  // larger size.
  num_reserved = buffer.reserve(30000, iovecs, NumIovecs);
  const void* third_slice = iovecs[1].mem_;
  EXPECT_EQ(2, num_reserved);
  EXPECT_EQ(first_slice, iovecs[0].mem_);
  EXPECT_EQ(12248, iovecs[0].len_);
  EXPECT_NE(second_slice, iovecs[1].mem_);
  EXPECT_EQ(30000 - iovecs[0].len_, iovecs[1].len_);
  expectSlices({{0, 12248, 12248}, {0, 8152, 8152}, {0, 20440, 20440}}, buffer);

  // Repeating a the reservation request for a smaller block returns the previous entry.
  num_reserved = buffer.reserve(16384, iovecs, NumIovecs);
  EXPECT_EQ(2, num_reserved);
  EXPECT_EQ(first_slice, iovecs[0].mem_);
  EXPECT_EQ(second_slice, iovecs[1].mem_);
  expectSlices({{0, 12248, 12248}, {0, 8152, 8152}, {0, 20440, 20440}}, buffer);

  // Repeat the larger reservation notice that it doesn't match the prior reservation for 30000
  // bytes.
  num_reserved = buffer.reserve(30000, iovecs, NumIovecs);
  EXPECT_EQ(2, num_reserved);
  EXPECT_EQ(first_slice, iovecs[0].mem_);
  EXPECT_EQ(12248, iovecs[0].len_);
  EXPECT_NE(second_slice, iovecs[1].mem_);
  EXPECT_NE(third_slice, iovecs[1].mem_);
  EXPECT_EQ(30000 - iovecs[0].len_, iovecs[1].len_);
  expectSlices({{0, 12248, 12248}, {0, 8152, 8152}, {0, 20440, 20440}, {0, 20440, 20440}}, buffer);

  // Commit the most recent reservation and verify the representation.
  buffer.commit(iovecs, num_reserved);
  expectSlices({{12248, 0, 12248}, {0, 8152, 8152}, {0, 20440, 20440}, {17752, 2688, 20440}},
               buffer);

  // Do another reservation.
  num_reserved = buffer.reserve(16384, iovecs, NumIovecs);
  EXPECT_EQ(2, num_reserved);
  expectSlices({{12248, 0, 12248},
                {0, 8152, 8152},
                {0, 20440, 20440},
                {17752, 2688, 20440},
                {0, 16344, 16344}},
               buffer);

  // And commit.
  buffer.commit(iovecs, num_reserved);
  expectSlices({{12248, 0, 12248},
                {0, 8152, 8152},
                {0, 20440, 20440},
                {20440, 0, 20440},
                {13696, 2648, 16344}},
               buffer);
}

TEST_F(OwnedImplTest, Search) {
  // Populate a buffer with a string split across many small slices, to
  // exercise edge cases in the search implementation.
  static const char* Inputs[] = {"ab", "a", "", "aaa", "b", "a", "aaa", "ab", "a"};
  Buffer::OwnedImpl buffer;
  for (const auto& input : Inputs) {
    buffer.appendSliceForTest(input);
  }
  EXPECT_STREQ("abaaaabaaaaaba", buffer.toString().c_str());

  EXPECT_EQ(-1, buffer.search("c", 1, 0));
  EXPECT_EQ(0, buffer.search("", 0, 0));
  EXPECT_EQ(buffer.length(), buffer.search("", 0, buffer.length()));
  EXPECT_EQ(-1, buffer.search("", 0, buffer.length() + 1));
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

TEST_F(OwnedImplTest, StartsWith) {
  // Populate a buffer with a string split across many small slices, to
  // exercise edge cases in the startsWith implementation.
  static const char* Inputs[] = {"ab", "a", "", "aaa", "b", "a", "aaa", "ab", "a"};
  Buffer::OwnedImpl buffer;
  for (const auto& input : Inputs) {
    buffer.appendSliceForTest(input);
  }
  EXPECT_STREQ("abaaaabaaaaaba", buffer.toString().c_str());

  EXPECT_FALSE(buffer.startsWith({"abaaaabaaaaabaXXX", 17}));
  EXPECT_FALSE(buffer.startsWith({"c", 1}));
  EXPECT_TRUE(buffer.startsWith({"", 0}));
  EXPECT_TRUE(buffer.startsWith({"a", 1}));
  EXPECT_TRUE(buffer.startsWith({"ab", 2}));
  EXPECT_TRUE(buffer.startsWith({"aba", 3}));
  EXPECT_TRUE(buffer.startsWith({"abaa", 4}));
  EXPECT_TRUE(buffer.startsWith({"abaaaab", 7}));
  EXPECT_TRUE(buffer.startsWith({"abaaaabaaaaaba", 14}));
  EXPECT_FALSE(buffer.startsWith({"ba", 2}));
}

TEST_F(OwnedImplTest, ToString) {
  Buffer::OwnedImpl buffer;
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

TEST_F(OwnedImplTest, AppendSliceForTest) {
  static constexpr size_t NumInputs = 3;
  static constexpr const char* Inputs[] = {"one", "2", "", "four", ""};
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

// Regression test for oss-fuzz issue
// https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=13263, where prepending
// an empty buffer resulted in a corrupted libevent internal state.
TEST_F(OwnedImplTest, PrependEmpty) {
  Buffer::OwnedImpl buf;
  Buffer::OwnedImpl other_buf;
  char input[] = "foo";
  BufferFragmentImpl frag(input, 3, nullptr);
  buf.addBufferFragment(frag);
  buf.prepend("");
  other_buf.move(buf, 1);
  buf.add("bar");
  EXPECT_EQ("oobar", buf.toString());
  buf.drain(5);
  EXPECT_EQ(0, buf.length());
}

// Regression test for oss-fuzz issues
// https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=14466, empty commit
// following a reserve resulted in a corrupted libevent internal state.
TEST_F(OwnedImplTest, ReserveZeroCommit) {
  BufferFragmentImpl frag("", 0, nullptr);
  Buffer::OwnedImpl buf;
  buf.addBufferFragment(frag);
  buf.prepend("bbbbb");
  buf.add("");
  constexpr uint32_t reserve_slices = 16;
  Buffer::RawSlice slices[reserve_slices];
  const uint32_t allocated_slices = buf.reserve(1280, slices, reserve_slices);
  for (uint32_t i = 0; i < allocated_slices; ++i) {
    slices[i].len_ = 0;
  }
  buf.commit(slices, allocated_slices);
  int pipe_fds[2] = {0, 0};
  ASSERT_EQ(::pipe(pipe_fds), 0);
  Network::IoSocketHandleImpl io_handle(pipe_fds[0]);
  ASSERT_EQ(::fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK), 0);
  ASSERT_EQ(::fcntl(pipe_fds[1], F_SETFL, O_NONBLOCK), 0);
  const uint32_t max_length = 1953;
  std::string data(max_length, 'e');
  const ssize_t rc = ::write(pipe_fds[1], data.data(), max_length);
  ASSERT_GT(rc, 0);
  const uint32_t previous_length = buf.length();
  Api::IoCallUint64Result result = buf.read(io_handle, max_length);
  ASSERT_EQ(result.rc_, static_cast<uint64_t>(rc));
  ASSERT_EQ(::close(pipe_fds[1]), 0);
  ASSERT_EQ(previous_length, buf.search(data.data(), rc, previous_length));
  EXPECT_EQ("bbbbb", buf.toString().substr(0, 5));
  expectSlices({{5, 0, 4056}, {1953, 2103, 4056}}, buf);
}

TEST_F(OwnedImplTest, ReadReserveAndCommit) {
  BufferFragmentImpl frag("", 0, nullptr);
  Buffer::OwnedImpl buf;
  buf.add("bbbbb");

  int pipe_fds[2] = {0, 0};
  ASSERT_EQ(::pipe(pipe_fds), 0);
  Network::IoSocketHandleImpl io_handle(pipe_fds[0]);
  ASSERT_EQ(::fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK), 0);
  ASSERT_EQ(::fcntl(pipe_fds[1], F_SETFL, O_NONBLOCK), 0);

  const uint32_t read_length = 32768;
  std::string data = "e";
  const ssize_t rc = ::write(pipe_fds[1], data.data(), data.size());
  ASSERT_GT(rc, 0);
  Api::IoCallUint64Result result = buf.read(io_handle, read_length);
  ASSERT_EQ(result.rc_, static_cast<uint64_t>(rc));
  ASSERT_EQ(::close(pipe_fds[1]), 0);
  EXPECT_EQ("bbbbbe", buf.toString());
  expectSlices({{6, 4050, 4056}}, buf);
}

TEST(OverflowDetectingUInt64, Arithmetic) {
  Logger::StderrSinkDelegate stderr_sink(Logger::Registry::getSink()); // For coverage build.
  OverflowDetectingUInt64 length;
  length += 1;
  length -= 1;
  length -= 0;
  EXPECT_DEATH(length -= 1, "underflow");
  uint64_t half = uint64_t(1) << 63;
  length += half;
  length += (half - 1); // length is now 2^64 - 1
  EXPECT_DEATH(length += 1, "overflow");
}

void TestBufferMove(uint64_t buffer1_length, uint64_t buffer2_length,
                    uint64_t expected_slice_count) {
  Buffer::OwnedImpl buffer1;
  buffer1.add(std::string(buffer1_length, 'a'));
  EXPECT_EQ(1, buffer1.getRawSlices(nullptr, 0));

  Buffer::OwnedImpl buffer2;
  buffer2.add(std::string(buffer2_length, 'b'));
  EXPECT_EQ(1, buffer2.getRawSlices(nullptr, 0));

  buffer1.move(buffer2);
  EXPECT_EQ(expected_slice_count, buffer1.getRawSlices(nullptr, 0));
  EXPECT_EQ(buffer1_length + buffer2_length, buffer1.length());
  // Make sure `buffer2` was drained.
  EXPECT_EQ(0, buffer2.length());
}

// Slice size large enough to prevent slice content from being coalesced into an existing slice
constexpr uint64_t kLargeSliceSize = 2048;

TEST_F(OwnedImplTest, MoveBuffersWithLargeSlices) {
  // Large slices should not be coalesced together
  TestBufferMove(kLargeSliceSize, kLargeSliceSize, 2);
}

TEST_F(OwnedImplTest, MoveBuffersWithSmallSlices) {
  // Small slices should be coalesced together
  TestBufferMove(1, 1, 1);
}

TEST_F(OwnedImplTest, MoveSmallSliceIntoLargeSlice) {
  // Small slices should be coalesced with a large one
  TestBufferMove(kLargeSliceSize, 1, 1);
}

TEST_F(OwnedImplTest, MoveLargeSliceIntoSmallSlice) {
  // Large slice should NOT be coalesced into the small one
  TestBufferMove(1, kLargeSliceSize, 2);
}

TEST_F(OwnedImplTest, MoveSmallSliceIntoNotEnoughFreeSpace) {
  // Small slice will not be coalesced if a previous slice does not have enough free space
  // Slice buffer sizes are allocated in 4Kb increments
  // Make first slice have 127 of free space (it is actually less as there is small overhead of the
  // OwnedSlice object) And second slice 128 bytes
  TestBufferMove(4096 - 127, 128, 2);
}

} // namespace
} // namespace Buffer
} // namespace Envoy
