#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/codec.h"

#include "test/common/buffer/utility.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/printers.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {
namespace {

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
  EXPECT_EQ(true, decoder.hasBufferedData());

  buffer.add(request_buffer.c_str() + 5);
  EXPECT_TRUE(decoder.decode(buffer, frames));
  EXPECT_EQ(static_cast<size_t>(0), buffer.length());
  EXPECT_EQ(static_cast<size_t>(1), frames.size());
  EXPECT_EQ(static_cast<uint32_t>(0), decoder.length());
  EXPECT_EQ(false, decoder.hasBufferedData());
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

// This test shows that null bytes in the bytestring successfully decode into a frame with length 0.
// Should this test really pass?
TEST(GrpcCodecTest, DecodeMultipleFramesInvalid) {
  // A frame constructed from null bytes followed by an invalid frame
  const std::string data("\000\000\000\000\0000000", 9);
  Buffer::OwnedImpl buffer(data.data(), data.size());

  size_t size = buffer.length();

  std::vector<Frame> frames;
  Decoder decoder;
  EXPECT_FALSE(decoder.decode(buffer, frames));
  // When the decoder doesn't successfully decode, it puts decoded frames up until
  // an invalid frame into output frame vector.
  EXPECT_EQ(1, frames.size());
  // Buffer does not get drained due to it returning false.
  EXPECT_EQ(size, buffer.length());
  // Only part of the buffer represented a frame. Thus, the frame length should not equal the buffer
  // length. The frame put into the output vector has no length.
  EXPECT_EQ(0, frames[0].length_);
}

// If there is a valid frame followed by an invalid frame, the decoder will successfully put the
// valid frame in the output and return false due to the invalid frame
TEST(GrpcCodecTest, DecodeValidFrameWithInvalidFrameAfterward) {
  // Decode a valid encoded structured request plus invalid data afterward
  helloworld::HelloRequest request;
  request.set_name("hello");

  Buffer::OwnedImpl buffer;
  std::array<uint8_t, 5> header;
  Encoder encoder;
  encoder.newFrame(GRPC_FH_DEFAULT, request.ByteSize(), header);
  buffer.add(header.data(), 5);
  buffer.add(request.SerializeAsString());
  buffer.add("000000", 6);
  size_t size = buffer.length();

  std::vector<Frame> frames;
  Decoder decoder;
  EXPECT_FALSE(decoder.decode(buffer, frames));
  // When the decoder doesn't successfully decode, it puts valid frames up until
  // an invalid frame into output frame vector.
  EXPECT_EQ(1, frames.size());
  // Buffer does not get drained due to it returning false.
  EXPECT_EQ(size, buffer.length());
  // Only part of the buffer represented a valid frame. Thus, the frame length should not equal the
  // buffer length.
  EXPECT_NE(size, frames[0].length_);
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

TEST(GrpcCodecTest, FrameInspectorTest) {
  {
    Buffer::OwnedImpl buffer;
    FrameInspector counter;
    EXPECT_EQ(0, counter.inspect(buffer));
    EXPECT_EQ(counter.state(), State::FhFlag);
    EXPECT_EQ(counter.frameCount(), 0);
  }

  {
    Buffer::OwnedImpl buffer;
    FrameInspector counter;
    Buffer::addSeq(buffer, {0});
    EXPECT_EQ(1, counter.inspect(buffer));
    EXPECT_EQ(counter.state(), State::FhLen0);
    EXPECT_EQ(counter.frameCount(), 1);
  }

  {
    Buffer::OwnedImpl buffer;
    FrameInspector counter;
    Buffer::addSeq(buffer, {1, 0, 0, 0, 1, 0xFF});
    EXPECT_EQ(1, counter.inspect(buffer));
    EXPECT_EQ(counter.state(), State::FhFlag);
    EXPECT_EQ(counter.frameCount(), 1);
  }

  {
    FrameInspector counter;
    Buffer::OwnedImpl buffer1;
    Buffer::addSeq(buffer1, {1, 0, 0, 0});
    EXPECT_EQ(1, counter.inspect(buffer1));
    EXPECT_EQ(counter.state(), State::FhLen3);
    EXPECT_EQ(counter.frameCount(), 1);
    Buffer::OwnedImpl buffer2;
    Buffer::addSeq(buffer2, {1, 0xFF});
    EXPECT_EQ(0, counter.inspect(buffer2));
    EXPECT_EQ(counter.frameCount(), 1);
  }

  {
    Buffer::OwnedImpl buffer;
    FrameInspector counter;
    Buffer::addSeq(buffer, {1, 0, 0, 0, 1, 0xFF});
    Buffer::addSeq(buffer, {0, 0, 0, 0, 2, 0xFF, 0xFF});
    EXPECT_EQ(2, counter.inspect(buffer));
    EXPECT_EQ(counter.state(), State::FhFlag);
    EXPECT_EQ(counter.frameCount(), 2);
  }

  {
    Buffer::OwnedImpl buffer1;
    Buffer::OwnedImpl buffer2;
    FrameInspector counter;
    // message spans two buffers
    Buffer::addSeq(buffer1, {1, 0, 0, 0, 2, 0xFF});
    Buffer::addSeq(buffer2, {0xFF, 0, 0, 0, 0, 2, 0xFF, 0xFF});
    EXPECT_EQ(1, counter.inspect(buffer1));
    EXPECT_EQ(1, counter.inspect(buffer2));
    EXPECT_EQ(counter.state(), State::FhFlag);
    EXPECT_EQ(counter.frameCount(), 2);
  }

  {
    Buffer::OwnedImpl buffer;
    FrameInspector counter;
    // Add longer byte sequence
    Buffer::addSeq(buffer, {1, 0, 0, 1, 0});
    Buffer::addRepeated(buffer, 1 << 8, 0xFF);
    // Start second message
    Buffer::addSeq(buffer, {0});
    EXPECT_EQ(2, counter.inspect(buffer));
    EXPECT_EQ(counter.state(), State::FhLen0);
    EXPECT_EQ(counter.frameCount(), 2);
  }

  {
    // two empty messages
    Buffer::OwnedImpl buffer;
    FrameInspector counter;
    Buffer::addRepeated(buffer, 10, 0);
    EXPECT_EQ(2, counter.inspect(buffer));
    EXPECT_EQ(counter.frameCount(), 2);
  }
}

} // namespace
} // namespace Grpc
} // namespace Envoy
