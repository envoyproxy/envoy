#include "common/grpc/transcoder_input_stream_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {
namespace {

class TranscoderInputStreamTest : public testing::Test {
public:
  TranscoderInputStreamTest() {
    Buffer::OwnedImpl buffer{"abcd"};
    stream_.Move(buffer);
  }

  std::string slice_data_{"abcd"};
  TranscoderInputStreamImpl stream_;

  const void* data_;
  int size_;
};

TEST_F(TranscoderInputStreamTest, Move) {
  Buffer::OwnedImpl buffer{"abcd"};
  stream_.Move(buffer);

  EXPECT_EQ(0, buffer.length());
  EXPECT_EQ(8, stream_.BytesAvailable());
}

TEST_F(TranscoderInputStreamTest, Next) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);
  EXPECT_EQ(0, memcmp(slice_data_.data(), data_, size_));
}

TEST_F(TranscoderInputStreamTest, TwoSlices) {
  Buffer::OwnedImpl buffer("efgh");

  stream_.Move(buffer);

  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);
  EXPECT_EQ(0, memcmp(slice_data_.data(), data_, size_));
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);
  EXPECT_EQ(0, memcmp("efgh", data_, size_));
}

TEST_F(TranscoderInputStreamTest, BackUp) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);

  stream_.BackUp(3);
  EXPECT_EQ(3, stream_.BytesAvailable());
  EXPECT_EQ(1, stream_.ByteCount());

  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(3, size_);
  EXPECT_EQ(0, memcmp("bcd", data_, size_));
  EXPECT_EQ(4, stream_.ByteCount());
}

TEST_F(TranscoderInputStreamTest, ByteCount) {
  EXPECT_EQ(0, stream_.ByteCount());
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, stream_.ByteCount());
}

TEST_F(TranscoderInputStreamTest, Finish) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(0, size_);
  stream_.Finish();
  EXPECT_FALSE(stream_.Next(&data_, &size_));

  Buffer::OwnedImpl buffer("efgh");
  stream_.Move(buffer);

  EXPECT_EQ(4, buffer.length());
}

} // namespace
} // namespace Grpc
} // namespace Envoy
