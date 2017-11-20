#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "common/buffer/buffer_impl.h"
#include "common/grpc/codec.h"

#include "test/proto/helloworld.pb.h"
#include "test/test_common/printers.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {

TEST(GrpcCodecTest, encodeHeader) {
  Encoder encoder;
  std::array<uint8_t, 5> buffer;

  encoder.newFrame(GRPC_FH_DEFAULT, 1, buffer);
  EXPECT_EQ(buffer[0], GRPC_FH_DEFAULT);
  EXPECT_EQ(buffer[1], 0);
  EXPECT_EQ(buffer[2], 0);
  EXPECT_EQ(buffer[3], 0);
  EXPECT_EQ(buffer[4], 1);

  encoder.newFrame(GRPC_FH_COMPRESSED, 1, buffer);
  EXPECT_EQ(buffer[0], GRPC_FH_COMPRESSED);
  EXPECT_EQ(buffer[1], 0);
  EXPECT_EQ(buffer[2], 0);
  EXPECT_EQ(buffer[3], 0);
  EXPECT_EQ(buffer[4], 1);

  encoder.newFrame(GRPC_FH_DEFAULT, 0x100, buffer);
  EXPECT_EQ(buffer[0], GRPC_FH_DEFAULT);
  EXPECT_EQ(buffer[1], 0);
  EXPECT_EQ(buffer[2], 0);
  EXPECT_EQ(buffer[3], 1);
  EXPECT_EQ(buffer[4], 0);

  encoder.newFrame(GRPC_FH_DEFAULT, 0x10000, buffer);
  EXPECT_EQ(buffer[0], GRPC_FH_DEFAULT);
  EXPECT_EQ(buffer[1], 0);
  EXPECT_EQ(buffer[2], 1);
  EXPECT_EQ(buffer[3], 0);
  EXPECT_EQ(buffer[4], 0);

  encoder.newFrame(GRPC_FH_DEFAULT, 0x1000000, buffer);
  EXPECT_EQ(buffer[0], GRPC_FH_DEFAULT);
  EXPECT_EQ(buffer[1], 1);
  EXPECT_EQ(buffer[2], 0);
  EXPECT_EQ(buffer[3], 0);
  EXPECT_EQ(buffer[4], 0);
}

TEST(GrpcCodecTest, decodeIncompleteFrame) {
  helloworld::HelloRequest request;
  request.set_name("hello");
  std::string request_buffer = request.SerializeAsString();

  Buffer::OwnedImpl buffer;
  std::array<uint8_t, 5> header;
  Encoder encoder;
  encoder.newFrame(GRPC_FH_DEFAULT, request.ByteSize(), header);
  buffer.add(header.data(), 5);
  buffer.add(request_buffer.c_str(), 5);

  std::vector<Frame> frames;
  Decoder decoder;
  EXPECT_TRUE(decoder.decode(buffer, frames));
  EXPECT_EQ(static_cast<size_t>(0), buffer.length());
  EXPECT_EQ(static_cast<size_t>(0), frames.size());
  EXPECT_EQ(static_cast<uint32_t>(request.ByteSize()), decoder.length());

  buffer.add(request_buffer.c_str() + 5);
  EXPECT_TRUE(decoder.decode(buffer, frames));
  EXPECT_EQ(static_cast<size_t>(0), buffer.length());
  EXPECT_EQ(static_cast<size_t>(1), frames.size());
  EXPECT_EQ(static_cast<uint32_t>(0), decoder.length());
  helloworld::HelloRequest decoded_request;
  EXPECT_TRUE(decoded_request.ParseFromArray(frames[0].data_->linearize(frames[0].data_->length()),
                                             frames[0].data_->length()));
  EXPECT_EQ("hello", decoded_request.name());
}

TEST(GrpcCodecTest, decodeInvalidFrame) {
  helloworld::HelloRequest request;
  request.set_name("hello");

  Buffer::OwnedImpl buffer;
  std::array<uint8_t, 5> header;
  Encoder encoder;
  encoder.newFrame(0b10u, request.ByteSize(), header);
  buffer.add(header.data(), 5);
  buffer.add(request.SerializeAsString());
  size_t size = buffer.length();

  std::vector<Frame> frames;
  Decoder decoder;
  EXPECT_FALSE(decoder.decode(buffer, frames));
  EXPECT_EQ(size, buffer.length());
}

TEST(GrpcCodecTest, decodeEmptyFrame) {
  Buffer::OwnedImpl buffer("\0\0\0\0", 5);

  Decoder decoder;
  std::vector<Frame> frames;
  EXPECT_TRUE(decoder.decode(buffer, frames));

  EXPECT_EQ(1, frames.size());
  EXPECT_EQ(0, frames[0].length_);
}

TEST(GrpcCodecTest, decodeSingleFrame) {
  helloworld::HelloRequest request;
  request.set_name("hello");

  Buffer::OwnedImpl buffer;
  std::array<uint8_t, 5> header;
  Encoder encoder;
  encoder.newFrame(GRPC_FH_DEFAULT, request.ByteSize(), header);
  buffer.add(header.data(), 5);
  buffer.add(request.SerializeAsString());

  std::vector<Frame> frames;
  Decoder decoder;
  EXPECT_TRUE(decoder.decode(buffer, frames));
  EXPECT_EQ(static_cast<size_t>(0), buffer.length());
  EXPECT_EQ(frames.size(), static_cast<uint64_t>(1));
  EXPECT_EQ(GRPC_FH_DEFAULT, frames[0].flags_);
  EXPECT_EQ(static_cast<uint64_t>(request.ByteSize()), frames[0].length_);

  helloworld::HelloRequest result;
  result.ParseFromArray(frames[0].data_->linearize(frames[0].data_->length()),
                        frames[0].data_->length());
  EXPECT_EQ("hello", result.name());
}

TEST(GrpcCodecTest, decodeMultipleFrame) {
  helloworld::HelloRequest request;
  request.set_name("hello");

  Buffer::OwnedImpl buffer;
  std::array<uint8_t, 5> header;
  Encoder encoder;
  encoder.newFrame(GRPC_FH_DEFAULT, request.ByteSize(), header);
  for (int i = 0; i < 1009; i++) {
    buffer.add(header.data(), 5);
    buffer.add(request.SerializeAsString());
  }

  std::vector<Frame> frames;
  Decoder decoder;
  EXPECT_TRUE(decoder.decode(buffer, frames));
  EXPECT_EQ(static_cast<size_t>(0), buffer.length());
  EXPECT_EQ(frames.size(), static_cast<uint64_t>(1009));
  for (Frame& frame : frames) {
    EXPECT_EQ(GRPC_FH_DEFAULT, frame.flags_);
    EXPECT_EQ(static_cast<uint64_t>(request.ByteSize()), frame.length_);

    helloworld::HelloRequest result;
    result.ParseFromArray(frame.data_->linearize(frame.data_->length()), frame.data_->length());
    EXPECT_EQ("hello", result.name());
  }
}

} // namespace Grpc
} // namespace Envoy
