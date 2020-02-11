#include "common/buffer/buffer_impl.h"
#include "common/buffer/zero_copy_input_stream_impl.h"

#include "test/common/buffer/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Buffer {
namespace {

class ZeroCopyInputStreamTest : public testing::Test {
public:
  ZeroCopyInputStreamTest() {
    Buffer::OwnedImpl buffer{"abcd"};
    stream_.move(buffer);
  }

  std::string slice_data_{"abcd"};
  ZeroCopyInputStreamImpl stream_;

  const void* data_;
  int size_;
};

TEST_F(ZeroCopyInputStreamTest, Move) {
  Buffer::OwnedImpl buffer{"abcd"};
  stream_.move(buffer);

  EXPECT_EQ(0, buffer.length());
}

TEST_F(ZeroCopyInputStreamTest, Next) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);
  EXPECT_EQ(0, memcmp(slice_data_.data(), data_, size_));
}

TEST_F(ZeroCopyInputStreamTest, TwoSlices) {
  // Make content larger than 512 bytes so it would not be coalesced when
  // moved into the stream_ buffer.
  Buffer::OwnedImpl buffer(std::string(1024, 'A'));
  stream_.move(buffer);

  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);
  EXPECT_EQ(0, memcmp(slice_data_.data(), data_, size_));
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(1024, size_);
  EXPECT_THAT(absl::string_view(static_cast<const char*>(data_), size_),
              testing::Each(testing::AllOf('A')));
}

TEST_F(ZeroCopyInputStreamTest, BackUp) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);

  stream_.BackUp(3);
  EXPECT_EQ(1, stream_.ByteCount());

  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(3, size_);
  EXPECT_EQ(0, memcmp("bcd", data_, size_));
  EXPECT_EQ(4, stream_.ByteCount());
}

TEST_F(ZeroCopyInputStreamTest, BackUpFull) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);

  stream_.BackUp(4);
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);
  EXPECT_EQ(0, memcmp("abcd", data_, size_));
  EXPECT_EQ(4, stream_.ByteCount());
}

TEST_F(ZeroCopyInputStreamTest, ByteCount) {
  EXPECT_EQ(0, stream_.ByteCount());
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, stream_.ByteCount());
}

TEST_F(ZeroCopyInputStreamTest, Finish) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(0, size_);
  stream_.finish();
  EXPECT_FALSE(stream_.Next(&data_, &size_));
}

} // namespace
} // namespace Buffer
} // namespace Envoy
