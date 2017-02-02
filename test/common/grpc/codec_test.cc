#include "common/buffer/buffer_impl.h"
#include "common/grpc/codec.h"
#include "test/generated/helloworld.pb.h"

namespace Grpc {

TEST(CodecTest, encodeHeader) {
  Encoder encoder;
  uint8_t buffer[5];

  encoder.NewFrame(GRPC_FH_DEFAULT, 1, buffer);
  EXPECT_EQ(buffer[0], GRPC_FH_DEFAULT);
  EXPECT_EQ(buffer[1], 0);
  EXPECT_EQ(buffer[2], 0);
  EXPECT_EQ(buffer[3], 0);
  EXPECT_EQ(buffer[4], 1);

  encoder.NewFrame(GRPC_FH_COMPRESSED, 1, buffer);
  EXPECT_EQ(buffer[0], GRPC_FH_COMPRESSED);
  EXPECT_EQ(buffer[1], 0);
  EXPECT_EQ(buffer[2], 0);
  EXPECT_EQ(buffer[3], 0);
  EXPECT_EQ(buffer[4], 1);

  encoder.NewFrame(GRPC_FH_DEFAULT, 0x100, buffer);
  EXPECT_EQ(buffer[0], GRPC_FH_DEFAULT);
  EXPECT_EQ(buffer[1], 0);
  EXPECT_EQ(buffer[2], 0);
  EXPECT_EQ(buffer[3], 1);
  EXPECT_EQ(buffer[4], 0);

  encoder.NewFrame(GRPC_FH_DEFAULT, 0x10000, buffer);
  EXPECT_EQ(buffer[0], GRPC_FH_DEFAULT);
  EXPECT_EQ(buffer[1], 0);
  EXPECT_EQ(buffer[2], 1);
  EXPECT_EQ(buffer[3], 0);
  EXPECT_EQ(buffer[4], 0);

  encoder.NewFrame(GRPC_FH_DEFAULT, 0x1000000, buffer);
  EXPECT_EQ(buffer[0], GRPC_FH_DEFAULT);
  EXPECT_EQ(buffer[1], 1);
  EXPECT_EQ(buffer[2], 0);
  EXPECT_EQ(buffer[3], 0);
  EXPECT_EQ(buffer[4], 0);
}

TEST(CodecTest, decodeInvalidFrame) {
  helloworld::HelloRequest request;
  request.set_name("hello");

  Buffer::OwnedImpl buffer;
  uint8_t header[5];
  Encoder encoder;
  encoder.NewFrame(0b10u, request.ByteSizeLong(), header);
  buffer.add(header, 5);
  buffer.add(request.SerializeAsString());

  std::vector<Frame> frames;

  Decoder decoder;
  EXPECT_EQ(false, decoder.Decode(buffer, &frames));
}

TEST(CodecTest, decodeSingleFrame) {
  helloworld::HelloRequest request;
  request.set_name("hello");

  Buffer::OwnedImpl buffer;
  uint8_t header[5];
  Encoder encoder;
  encoder.NewFrame(GRPC_FH_DEFAULT, request.ByteSizeLong(), header);
  buffer.add(header, 5);
  buffer.add(request.SerializeAsString());

  std::vector<Frame> frames;

  Decoder decoder;
  decoder.Decode(buffer, &frames);
  EXPECT_EQ(frames.size(), 1);
  EXPECT_EQ(GRPC_FH_DEFAULT, frames[0].flags);
  EXPECT_EQ(request.ByteSizeLong(), frames[0].length);

  helloworld::HelloRequest result;
  result.ParseFromArray(frames[0].data->linearize(frames[0].data->length()),
                        frames[0].data->length());
  EXPECT_EQ("hello", result.name());
}

TEST(CodecTest, decodeMultipleFrame) {
  helloworld::HelloRequest request;
  request.set_name("hello");

  Buffer::OwnedImpl buffer;
  uint8_t header[5];
  Encoder encoder;
  encoder.NewFrame(GRPC_FH_DEFAULT, request.ByteSizeLong(), header);
  for (int i = 0; i < 1009; i++) {
    buffer.add(header, 5);
    buffer.add(request.SerializeAsString());
  }

  std::vector<Frame> frames;

  Decoder decoder;
  decoder.Decode(buffer, &frames);
  EXPECT_EQ(frames.size(), 1009);
  for (Frame& frame : frames) {
    EXPECT_EQ(GRPC_FH_DEFAULT, frame.flags);
    EXPECT_EQ(request.ByteSizeLong(), frame.length);

    helloworld::HelloRequest result;
    result.ParseFromArray(frame.data->linearize(frame.data->length()),
                          frame.data->length());
    EXPECT_EQ("hello", result.name());
  }
}
}  // Grpc
