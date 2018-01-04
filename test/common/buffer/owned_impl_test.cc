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

} // namespace
} // namespace Buffer
} // namespace Envoy
