#include "common/buffer/buffer_impl.h"

#include "gtest/gtest.h"

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
  buffer.addBufferFragment(&frag);
  EXPECT_EQ(11, buffer.length());

  buffer.drain(11);
  EXPECT_EQ(0, buffer.length());
}

TEST_F(OwnedImplTest, addBufferFragmentWithCleanup) {
  char input[] = "hello world";
  BufferFragmentImpl frag(input, 11,
                          [this](const void*, size_t) { release_callback_called_ = true; });
  Buffer::OwnedImpl buffer;
  buffer.addBufferFragment(&frag);
  EXPECT_EQ(11, buffer.length());

  buffer.drain(5);
  EXPECT_EQ(6, buffer.length());
  EXPECT_FALSE(release_callback_called_);

  buffer.drain(6);
  EXPECT_EQ(0, buffer.length());
  EXPECT_TRUE(release_callback_called_);
}

TEST_F(OwnedImplTest, AddBufferFragmentTwoBuffers) {
  char input[] = "hello world";
  BufferFragmentImpl frag(input, 11,
                          [this](const void*, size_t) { release_callback_called_ = true; });
  Buffer::OwnedImpl buffer1;
  Buffer::OwnedImpl buffer2;
  buffer1.addBufferFragment(&frag);

  buffer2.addBufferFragment(&frag);
  frag.incRef();

  EXPECT_EQ(11, buffer1.length());
  EXPECT_EQ(11, buffer2.length());

  buffer1.drain(5);
  buffer2.drain(5);
  EXPECT_EQ(6, buffer1.length());
  EXPECT_EQ(6, buffer2.length());
  EXPECT_FALSE(release_callback_called_);

  buffer1.drain(6);
  EXPECT_EQ(0, buffer1.length());
  EXPECT_FALSE(release_callback_called_);

  buffer2.drain(6);
  EXPECT_EQ(0, buffer2.length());
  EXPECT_TRUE(release_callback_called_);
}

} // namespace
} // namespace Buffer
} // namespace Envoy
