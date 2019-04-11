#include "common/buffer/buffer_impl.h"
#include "common/buffer/zero_copy_input_stream_impl.h"

#include "test/common/buffer/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Buffer {
namespace {

class ZeroCopyInputStreamTest : public BufferImplementationParamTest {
public:
  ZeroCopyInputStreamTest() {
    Buffer::OwnedImpl buffer{"abcd"};
    verifyImplementation(buffer);
    stream_.move(buffer);
  }

  std::string slice_data_{"abcd"};
  ZeroCopyInputStreamImpl stream_;

  const void* data_;
  int size_;
};

TEST_P(ZeroCopyInputStreamTest, Move) {
  Buffer::OwnedImpl buffer{"abcd"};
  verifyImplementation(buffer);
  stream_.move(buffer);

  EXPECT_EQ(0, buffer.length());
}

TEST_P(ZeroCopyInputStreamTest, Next) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);
  EXPECT_EQ(0, memcmp(slice_data_.data(), data_, size_));
}

TEST_P(ZeroCopyInputStreamTest, TwoSlices) {
  Buffer::OwnedImpl buffer("efgh");
  verifyImplementation(buffer);

  stream_.move(buffer);

  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);
  EXPECT_EQ(0, memcmp(slice_data_.data(), data_, size_));
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);
  EXPECT_EQ(0, memcmp("efgh", data_, size_));
}

TEST_P(ZeroCopyInputStreamTest, BackUp) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);

  stream_.BackUp(3);
  EXPECT_EQ(1, stream_.ByteCount());

  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(3, size_);
  EXPECT_EQ(0, memcmp("bcd", data_, size_));
  EXPECT_EQ(4, stream_.ByteCount());
}

TEST_P(ZeroCopyInputStreamTest, BackUpFull) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);

  stream_.BackUp(4);
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);
  EXPECT_EQ(0, memcmp("abcd", data_, size_));
  EXPECT_EQ(4, stream_.ByteCount());
}

TEST_P(ZeroCopyInputStreamTest, ByteCount) {
  EXPECT_EQ(0, stream_.ByteCount());
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, stream_.ByteCount());
}

TEST_P(ZeroCopyInputStreamTest, Finish) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(0, size_);
  stream_.finish();
  EXPECT_FALSE(stream_.Next(&data_, &size_));
}

} // namespace
} // namespace Buffer
} // namespace Envoy
