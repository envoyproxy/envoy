#include "common/api/os_sys_calls_impl.h"
#include "common/buffer/buffer_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Buffer {
namespace {

class OwnedImplTest : public testing::Test {
public:
  OwnedImplTest() {}

  bool release_callback_called_ = false;
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
  char input[] = "hello world";
  BufferFragmentImpl frag(input, 11, [this](const void*, size_t, const BufferFragmentImpl*) {
    release_callback_called_ = true;
  });
  Buffer::OwnedImpl buffer;
  buffer.addBufferFragment(frag);
  EXPECT_EQ(11, buffer.length());

  buffer.drain(5);
  EXPECT_EQ(6, buffer.length());
  EXPECT_FALSE(release_callback_called_);

  buffer.drain(6);
  EXPECT_EQ(0, buffer.length());
  EXPECT_TRUE(release_callback_called_);
}

TEST_F(OwnedImplTest, AddBufferFragmentDynamicAllocation) {
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
  buffer.addBufferFragment(*frag);
  EXPECT_EQ(11, buffer.length());

  buffer.drain(5);
  EXPECT_EQ(6, buffer.length());
  EXPECT_FALSE(release_callback_called_);

  buffer.drain(6);
  EXPECT_EQ(0, buffer.length());
  EXPECT_TRUE(release_callback_called_);
}

TEST_F(OwnedImplTest, Prepend) {
  std::string suffix = "World!", prefix = "Hello, ";
  Buffer::OwnedImpl buffer;
  buffer.add(suffix);
  buffer.prepend(prefix);

  EXPECT_EQ(suffix.size() + prefix.size(), buffer.length());
  EXPECT_EQ(prefix + suffix, buffer.toString());
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

TEST_F(OwnedImplTest, Read) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Buffer::OwnedImpl buffer;
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

} // namespace
} // namespace Buffer
} // namespace Envoy
