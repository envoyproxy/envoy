#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/grpc_json_transcoder/transcoder_input_stream_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {
namespace {

class TranscoderInputStreamTest : public testing::Test {
public:
  TranscoderInputStreamTest() {
    Buffer::OwnedImpl buffer{"abcd"};
    stream_.move(buffer);
  }

  std::string slice_data_{"abcd"};
  TranscoderInputStreamImpl stream_;

  const void* data_;
  int size_;
};

TEST_F(TranscoderInputStreamTest, BytesAvailable) {
  Buffer::OwnedImpl buffer{"abcd"};

  stream_.move(buffer);
  EXPECT_EQ(8, stream_.BytesAvailable());
}

TEST_F(TranscoderInputStreamTest, TwoSlices) {
  Buffer::OwnedImpl buffer("efghi");

  stream_.move(buffer);
  EXPECT_EQ(9, stream_.BytesAvailable());
}

TEST_F(TranscoderInputStreamTest, BackUp) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);

  stream_.BackUp(3);
  EXPECT_EQ(3, stream_.BytesAvailable());
}

} // namespace
} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
