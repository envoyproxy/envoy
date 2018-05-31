#include "common/api/os_sys_calls_impl.h"
#include "common/buffer/buffer_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::_;

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

TEST_F(OwnedImplTest, addBufferFragmentWithCleanup) {
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

TEST_F(OwnedImplTest, addBufferFragmentDynamicAllocation) {
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

TEST_F(OwnedImplTest, write) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Buffer::OwnedImpl buffer;
  buffer.add("example");
  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(7));
  int rc = buffer.write(-1);
  EXPECT_EQ(7, rc);
  EXPECT_EQ(0, buffer.length());

  buffer.add("example");
  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(6));
  rc = buffer.write(-1);
  EXPECT_EQ(6, rc);
  EXPECT_EQ(1, buffer.length());

  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(0));
  rc = buffer.write(-1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, buffer.length());

  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(-1));
  rc = buffer.write(-1);
  EXPECT_EQ(-1, rc);
  EXPECT_EQ(1, buffer.length());

  EXPECT_CALL(os_sys_calls, writev(_, _, _)).WillOnce(Return(1));
  rc = buffer.write(-1);
  EXPECT_EQ(1, rc);
  EXPECT_EQ(0, buffer.length());

  EXPECT_CALL(os_sys_calls, writev(_, _, _)).Times(0);
  rc = buffer.write(-1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, buffer.length());
}

TEST_F(OwnedImplTest, read) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Buffer::OwnedImpl buffer;
  EXPECT_CALL(os_sys_calls, readv(_, _, _)).WillOnce(Return(0));
  int rc = buffer.read(-1, 100);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, buffer.length());

  EXPECT_CALL(os_sys_calls, readv(_, _, _)).WillOnce(Return(-1));
  rc = buffer.read(-1, 100);
  EXPECT_EQ(-1, rc);
  EXPECT_EQ(0, buffer.length());

  EXPECT_CALL(os_sys_calls, readv(_, _, _)).Times(0);
  rc = buffer.read(-1, 0);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, buffer.length());
}

} // namespace
} // namespace Buffer
} // namespace Envoy
