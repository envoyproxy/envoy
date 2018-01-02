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

TEST_F(OwnedImplTest, AddReferenceNoCleanup) {
  char input[] = "hello world";
  Buffer::OwnedImpl buffer;
  buffer.addReference(input, 11, nullptr, nullptr);
  EXPECT_EQ(11, buffer.length());

  buffer.drain(11);
  EXPECT_EQ(0, buffer.length());
}

TEST_F(OwnedImplTest, AddReferenceWithCleanup) {
  char input[] = "hello world";
  Buffer::OwnedImpl buffer;
  buffer.addReference(input, 11, [](const void*, size_t, void* arg) {
    (static_cast<OwnedImplTest*>(arg))->release_callback_called_ = true;
  }, this);
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