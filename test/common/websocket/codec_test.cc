#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/websocket/codec.h"

#include "test/common/buffer/utility.h"
#include "test/test_common/printers.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace WebSocket {
namespace {

// Default payload buffer length for tests
constexpr uint64_t kDefaultPayloadBufferLength = 1048576;

bool areFramesEqual(const Frame& a, const Frame& b) {
  return a.final_fragment_ == b.final_fragment_ && a.opcode_ == b.opcode_ &&
         a.masking_key_ == b.masking_key_ && a.payload_length_ == b.payload_length_ &&
         ((a.payload_ == nullptr && b.payload_ == nullptr) ||
          (a.payload_->toString() == b.payload_->toString()));
}

Buffer::InstancePtr makeBuffer(absl::string_view data) {
  return std::make_unique<Buffer::OwnedImpl>(data.data(), data.length());
}

TEST(WebSocketCodecTest, EncodeFrameHeader) {
  Encoder encoder;
  absl::optional<std::vector<uint8_t>> header = absl::nullopt;
  std::vector<uint8_t> expected_header;

  header = encoder.encodeFrameHeader({true, kFrameOpcodeText, absl::nullopt, 5, nullptr});
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ(header->size(), 2);
  expected_header = {0x81, 0x05};
  EXPECT_EQ(header, expected_header);

  header->clear();
  header = encoder.encodeFrameHeader({true, kFrameOpcodeText, 0x37fa213d, 5, nullptr});
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ(header->size(), 6);
  expected_header = {0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d};
  EXPECT_EQ(header, expected_header);

  header->clear();
  header = encoder.encodeFrameHeader({false, kFrameOpcodeContinuation, 0x3c332a16, 5, nullptr});
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ(header->size(), 6);
  expected_header = {0x00, 0x85, 0x3c, 0x33, 0x2a, 0x16};
  EXPECT_EQ(header, expected_header);

  header->clear();
  header = encoder.encodeFrameHeader({true, kFrameOpcodeBinary, absl::nullopt, 256, nullptr});
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ(header->size(), 4);
  expected_header = {0x82, 0x7e, 0x01, 0x00};
  EXPECT_EQ(header, expected_header);

  header->clear();
  header = encoder.encodeFrameHeader({true, kFrameOpcodeBinary, 0x37fa213d, 256, nullptr});
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ(header->size(), 8);
  expected_header = {0x82, 0xfe, 0x01, 0x00, 0x37, 0xfa, 0x21, 0x3d};
  EXPECT_EQ(header, expected_header);

  header->clear();
  header = encoder.encodeFrameHeader({true, kFrameOpcodeBinary, absl::nullopt, 77777, nullptr});
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ(header->size(), 10);
  expected_header = {0x82, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x2f, 0xd1};
  EXPECT_EQ(header, expected_header);

  header->clear();
  header = encoder.encodeFrameHeader({true, kFrameOpcodeBinary, 0x37fa213d, 77777, nullptr});
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ(header->size(), 14);
  expected_header = {0x82, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x01, 0x2f, 0xd1, 0x37, 0xfa, 0x21, 0x3d};
  EXPECT_EQ(header, expected_header);
}

// Encode frame with invalid opcode
TEST(WebSocketCodecTest, EncodeInvalidFrame) {
  Encoder encoder;
  absl::optional<std::vector<uint8_t>> header = absl::nullopt;
  std::vector<uint8_t> expected_header;

  header = encoder.encodeFrameHeader({true, 0xc, absl::nullopt, 10, nullptr});
  ASSERT_FALSE(header.has_value());
}

// A single-frame unmasked text message
// 0x81 0x05 0x48 0x65 0x6c 0x6c 0x6f (contains "Hello")
//                   H        e        l        l        o
// 10000001 00000101 01001000 01100101 01101100 01101100 01101111
// fin - 1
// opcode - 0x1 - text data
// mask bit - 0
// length - 5 bytes 0000101
TEST(WebSocketCodecTest, DecodeSingleUnmaskedFrame) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f});

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);
  EXPECT_TRUE(areFramesEqual(frames.value()[0],
                             {true, kFrameOpcodeText, absl::nullopt, 5, makeBuffer("Hello")}));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

TEST(WebSocketCodecTest, DecodeSingleUnmaskedFrameExceptPayload) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f});

  Decoder decoder(0);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);
  EXPECT_TRUE(
      areFramesEqual(frames.value()[0], {true, kFrameOpcodeText, absl::nullopt, 5, nullptr}));
}

// A fragmented unmasked text message
//
// frame-1: 0x01 0x03 0x48 0x65 0x6c (contains "Hel")
// 00000001 00000011 H e l
// fin - 0 (not final frame - either first or not last)
// text frame - 1 (type info is in initial frame)
//
// frame-2: 0x80 0x02 0x6c 0x6f (contains "lo")
// 10000000 00000010 l o
// fin - 1 (final frame)
// continuation frame
TEST(WebSocketCodecTest, DecodeTwoUnmaskedFrames) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x01, 0x03, 0x48, 0x65, 0x6c});
  Buffer::addSeq(buffer, {0x80, 0x02, 0x6c, 0x6f});

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(2, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0],
                             {false, kFrameOpcodeText, absl::nullopt, 3, makeBuffer("Hel")}));
  EXPECT_TRUE(areFramesEqual(frames.value()[1],
                             {true, kFrameOpcodeContinuation, absl::nullopt, 2, makeBuffer("lo")}));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

// A fragmented and unfragmented text message
TEST(WebSocketCodecTest, DecodeThreeFrames) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x01, 0x03, 0x48, 0x65, 0x6c});
  Buffer::addSeq(buffer, {0x80, 0x02, 0x6c, 0x6f});
  Buffer::addSeq(buffer, {0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f});

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(3, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0],
                             {false, kFrameOpcodeText, absl::nullopt, 3, makeBuffer("Hel")}));
  EXPECT_TRUE(areFramesEqual(frames.value()[1],
                             {true, kFrameOpcodeContinuation, absl::nullopt, 2, makeBuffer("lo")}));
  EXPECT_TRUE(areFramesEqual(frames.value()[2], {true, kFrameOpcodeText, absl::nullopt, 5,
                                                 makeBuffer("\x48\x65\x6c\x6c\x6f")}));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

TEST(WebSocketCodecTest, DecodeThreeFramesExceptPayload) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x01, 0x03, 0x48, 0x65, 0x6c});
  Buffer::addSeq(buffer, {0x80, 0x02, 0x6c, 0x6f});
  Buffer::addSeq(buffer, {0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f});

  Decoder decoder(0);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(3, frames->size());
  EXPECT_TRUE(
      areFramesEqual(frames.value()[0], {false, kFrameOpcodeText, absl::nullopt, 3, nullptr}));
  EXPECT_TRUE(areFramesEqual(frames.value()[1],
                             {true, kFrameOpcodeContinuation, absl::nullopt, 2, nullptr}));
  EXPECT_TRUE(
      areFramesEqual(frames.value()[2], {true, kFrameOpcodeText, absl::nullopt, 5, nullptr}));
}

// A single-frame masked text message ("Hello")
// 0x81 0x85 0x37 0xfa 0x21 0x3d 0x7f 0x9f 0x4d 0x51 0x58
//           M1   M2   M3   M4   B1   B2   B3   B4   B5
// fin - 1
// opcode - 0x1 - text data
// mask bit - 1
// length - 5 bytes 0000101
//
// H - M1 XOR B1   01001000 = 00110111 xor 01111111
// e - M2 XOR B2
// l - M3 XOR B3
// l - M4 XOR B4
// o - M1 XOR B5
//
// j = i MOD 4
// O(i) = M(j) XOR B(i)
TEST(WebSocketCodecTest, DecodeSingleMaskedFrame) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58});

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0], {true, kFrameOpcodeText, 0x37fa213d, 5,
                                                 makeBuffer("\x7f\x9f\x4d\x51\x58")}));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

// Unmasked Ping request and masked Ping response
//
// 0x89 0x05 0x48 0x65 0x6c 0x6c 0x6f (contains a body of "Hello",
// but the contents of the body are arbitrary)
//
// 0x8a 0x85 0x37 0xfa 0x21 0x3d 0x7f 0x9f 0x4d 0x51 0x58
// (contains a body of "Hello", matching the body of the ping)
TEST(WebSocketCodecTest, DecodePingFrames) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x89, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f});
  Buffer::addSeq(buffer, {0x8a, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58});

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(2, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0],
                             {true, kFrameOpcodePing, absl::nullopt, 5, makeBuffer("Hello")}));
  EXPECT_TRUE(areFramesEqual(frames.value()[1], {true, kFrameOpcodePong, 0x37fa213d, 5,
                                                 makeBuffer("\x7f\x9f\x4d\x51\x58")}));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

// 256 bytes binary message in a single unmasked frame
// 0x82 0x7E 0x0100 [256 bytes of binary data]
TEST(WebSocketCodecTest, Decode16BitBinaryUnmaskedFrame) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x82, 0x7e, 0x01, 0x00});

  std::initializer_list<uint8_t> payload = {
      0xd9, 0xac, 0x96, 0x06, 0xc2, 0x83, 0x41, 0x31, 0x16, 0x6c, 0x27, 0x6e, 0x3c, 0xc9, 0x6b,
      0x7f, 0x12, 0x32, 0xb4, 0x3f, 0x19, 0x9a, 0x0a, 0x23, 0x6e, 0xfb, 0xb0, 0xda, 0x40, 0x4a,
      0xd2, 0xf4, 0xc0, 0xee, 0xa3, 0xc2, 0x3e, 0x7c, 0x96, 0x26, 0x3f, 0x14, 0x34, 0xee, 0xfe,
      0xa5, 0x42, 0x23, 0x5b, 0xc8, 0x43, 0xe4, 0xd4, 0xf7, 0xde, 0x79, 0xa6, 0xf9, 0x40, 0xfa,
      0xfe, 0xb0, 0x8e, 0xe7, 0x26, 0xa9, 0xa3, 0x6f, 0x07, 0x5c, 0x0a, 0xf1, 0x78, 0xc9, 0xb7,
      0x34, 0x80, 0xd9, 0x22, 0xdd, 0x68, 0x0a, 0x34, 0xba, 0x84, 0x64, 0x4d, 0x62, 0x98, 0x44,
      0xce, 0x92, 0x0c, 0x16, 0xb1, 0xa5, 0x9b, 0x23, 0xb2, 0xf5, 0x6a, 0x99, 0xc7, 0x9f, 0x89,
      0xaf, 0x4b, 0xe8, 0x7a, 0x5e, 0x7d, 0xf5, 0xe6, 0x6a, 0x3e, 0x4a, 0xc9, 0x5e, 0x70, 0xbf,
      0x04, 0x3c, 0x33, 0x2a, 0x16, 0xc5, 0xb4, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2, 0x1d, 0xe1,
      0xb1, 0x8c, 0xde, 0xa8, 0x35, 0xd7, 0xda, 0x3a, 0x07, 0xb2, 0xf7, 0x71, 0x54, 0x31, 0x7a,
      0x1f, 0x45, 0x1c, 0xf9, 0x82, 0xd5, 0x47, 0xfe, 0xc7, 0xb7, 0xd0, 0x75, 0xe6, 0x2e, 0x56,
      0x91, 0x66, 0xb4, 0x89, 0x66, 0xa3, 0xd4, 0xaf, 0x15, 0x3e, 0x7d, 0xa0, 0xd6, 0xca, 0xf9,
      0xc7, 0xd9, 0x5d, 0xc6, 0x33, 0x9d, 0xd5, 0x41, 0x22, 0x0f, 0x18, 0xc1, 0xe7, 0xff, 0x8f,
      0x2e, 0x6c, 0xaa, 0x81, 0x0f, 0x4e, 0xa5, 0xb6, 0x25, 0x4e, 0x8e, 0x3b, 0x25, 0x1e, 0x14,
      0x58, 0x12, 0x5f, 0xfc, 0xe8, 0xb8, 0xc8, 0xc9, 0xd0, 0xa1, 0x53, 0x04, 0xdb, 0x91, 0xd6,
      0x9a, 0xc6, 0xa1, 0xe8, 0xd7, 0xb0, 0x49, 0x6d, 0xd8, 0x00, 0x96, 0x41, 0x8d, 0x54, 0xdf,
      0x0a, 0x87, 0xea, 0xe7, 0x4c, 0xdf, 0x16, 0x9a, 0x30, 0xa0, 0x26, 0x1c, 0xc4, 0x2c, 0xad,
      0xfd};
  Buffer::addSeq(buffer, payload);

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(areFramesEqual(
      frames.value()[0], {true, kFrameOpcodeBinary, absl::nullopt, 256,
                          std::make_unique<Buffer::OwnedImpl>(payload.begin(), payload.size())}));
}

// 126 bytes binary message in a single masked frame
TEST(WebSocketCodecTest, Decode16BitBinaryMaskedFrame) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x82, 0xfe, 0x00, 0x7e, 0xa1, 0xe8, 0xd7, 0xb0});

  std::initializer_list<uint8_t> payload = {
      0x75, 0xe6, 0x2e, 0x56, 0x91, 0x66, 0xb4, 0x89, 0x66, 0xa3, 0xd4, 0xaf, 0x15, 0x3e,
      0x7d, 0xa0, 0xd6, 0xca, 0xf9, 0xc7, 0xd9, 0x5d, 0xc6, 0x33, 0x9d, 0xd5, 0x41, 0x22,
      0x0f, 0x18, 0xc1, 0xe7, 0xff, 0x8f, 0x2e, 0x6c, 0xaa, 0x81, 0x0f, 0x4e, 0xa5, 0xb6,
      0x25, 0x4e, 0x8e, 0x3b, 0x25, 0x1e, 0x14, 0x58, 0x12, 0x5f, 0xfc, 0xe8, 0xb8, 0xc8,
      0xc9, 0xd0, 0xa1, 0x53, 0x04, 0xdb, 0x91, 0xd6, 0x9a, 0xc6, 0x49, 0x6d, 0x96, 0x41,
      0x8d, 0x9a, 0xd8, 0x00, 0x96, 0x41, 0x8d, 0x54, 0xdf, 0x0a, 0x87, 0xea, 0xe7, 0x4c,
      0xdf, 0x16, 0x9a, 0x30, 0xa0, 0x26, 0x1c, 0xc4, 0x2c, 0xad, 0xfd, 0xd0, 0x2a, 0x16,
      0xc5, 0xb4, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2, 0x1d, 0xe1, 0x8c, 0xde, 0xa8, 0x35,
      0xd7, 0xda, 0x3a, 0x07, 0xb2, 0xf7, 0x71, 0x54, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2,
  };
  Buffer::addSeq(buffer, payload);

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(areFramesEqual(
      frames.value()[0], {true, kFrameOpcodeBinary, 0xa1e8d7b0, 126,
                          std::make_unique<Buffer::OwnedImpl>(payload.begin(), payload.size())}));
}

TEST(WebSocketCodecTest, Decode16BitBinaryMaskedFrameExceptPayload) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(
      buffer,
      {0x82, 0xfe, 0x00, 0x7e, 0xa1, 0xe8, 0xd7, 0xb0, 0xd7, 0xda, 0x3a, 0x07, 0xb2, 0xf7, 0x71,
       0x54, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2, 0x75, 0xe6, 0x2e, 0x56, 0x91, 0x66, 0xb4, 0x89,
       0x66, 0xa3, 0xd4, 0xaf, 0x15, 0x3e, 0x7d, 0xa0, 0xd6, 0xca, 0xf9, 0xc7, 0xd9, 0x5d, 0xc6,
       0x33, 0x9d, 0xd5, 0x41, 0x22, 0x0f, 0x18, 0xc1, 0xe7, 0xff, 0x8f, 0x2e, 0x6c, 0xaa, 0x81,
       0x0f, 0x4e, 0xa5, 0xb6, 0x25, 0x4e, 0x8e, 0x3b, 0x25, 0x1e, 0x14, 0x58, 0x12, 0x5f, 0xfc,
       0xe8, 0xb8, 0xc8, 0xc9, 0xd0, 0xa1, 0x53, 0x04, 0xdb, 0x91, 0xd6, 0x9a, 0xc6, 0x49, 0x6d,
       0x96, 0x41, 0x8d, 0x9a, 0xd8, 0x00, 0x96, 0x41, 0x8d, 0x54, 0xdf, 0x0a, 0x87, 0xea, 0xe7,
       0x4c, 0xdf, 0x16, 0x9a, 0x30, 0xa0, 0x26, 0x1c, 0xc4, 0x2c, 0xad, 0xfd, 0xd0, 0x2a, 0x16,
       0xc5, 0xb4, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2, 0x1d, 0xe1, 0x8c, 0xde, 0xa8, 0x35});

  Decoder decoder(0);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(
      areFramesEqual(frames.value()[0], {true, kFrameOpcodeBinary, 0xa1e8d7b0, 126, nullptr}));
}

// 64KiB binary message in a single unmasked frame
// 0x82 0x7f 0x0000000000010000 [65536 bytes of binary data]
TEST(WebSocketCodecTest, Decode64BitBinaryUnmaskedFrame) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x82, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00});

  // Add test payload 65536 bytes
  for (uint16_t i = 0; i < 4096; ++i) {
    Buffer::addSeq(buffer, {0x16, 0xc5, 0xb4, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2, 0x1d, 0xe1, 0x8c,
                            0xde, 0xa8, 0x35, 0xde});
  }

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());

  std::vector<uint8_t> payload;
  for (uint16_t i = 0; i < 4096; ++i) {
    payload.insert(payload.end(), {0x16, 0xc5, 0xb4, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2, 0x1d, 0xe1,
                                   0x8c, 0xde, 0xa8, 0x35, 0xde});
  }
  EXPECT_TRUE(areFramesEqual(
      frames.value()[0], {true, kFrameOpcodeBinary, absl::nullopt, 65536,
                          std::make_unique<Buffer::OwnedImpl>(payload.data(), payload.size())}));
}

// Frame with only frame header data
TEST(WebSocketCodecTest, DecodeIncompleteFrameWithgOnlyFrameHeader) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x81, 0x05});

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);
  ASSERT_FALSE(frames.has_value());
}

// Frame only includes up to first few length bytes of the frame
TEST(WebSocketCodecTest, DecodeIncompleteFrameUptoPartialLength) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x82, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00});

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);
  ASSERT_FALSE(frames.has_value());
}

// Masked frames with buffer includes only up to half of the masking key
TEST(WebSocketCodecTest, DecodeIncompleteFrameUptoPartialMaskingKey) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x82, 0xfe, 0x00, 0x7e, 0xa1, 0xe8, 0xd7});

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);
  ASSERT_FALSE(frames.has_value());
}

// Frame with incomplete payload
TEST(WebSocketCodecTest, DecodeIncompleteFrameWithPartialPayload) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x89, 0x05, 0x48, 0x65, 0x6c, 0x6c});

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);
  ASSERT_FALSE(frames.has_value());
}

// Check frame decoding iteratively
TEST(WebSocketCodecTest, DecodeFrameIteratively) {
  // A complete frame - 0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f
  absl::optional<std::vector<Frame>> frames = absl::nullopt;
  Decoder decoder(kDefaultPayloadBufferLength);

  Buffer::OwnedImpl frame_part_1;
  Buffer::addSeq(frame_part_1, {0x81, 0x05});

  Buffer::OwnedImpl frame_part_2;
  Buffer::addSeq(frame_part_2, {0x48, 0x65, 0x6c, 0x6c});

  Buffer::OwnedImpl frame_part_3;
  Buffer::addSeq(frame_part_3, {0x6f});

  frames = decoder.decode(frame_part_1);
  ASSERT_FALSE(frames.has_value());

  frames = decoder.decode(frame_part_2);
  ASSERT_FALSE(frames.has_value());

  frames = decoder.decode(frame_part_3);
  ASSERT_TRUE(frames.has_value());

  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0],
                             {true, kFrameOpcodeText, absl::nullopt, 5, makeBuffer("Hello")}));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

TEST(WebSocketCodecTest, DecodeTwoFramesInterleavedInDecodeCalls) {
  // frame-1: 0x01 0x03 0x48 0x65 0x6c (contains "Hel")
  // frame-2: 0x80 0x02 0x6c 0x6f (contains "lo")
  absl::optional<std::vector<Frame>> frames = absl::nullopt;
  Decoder decoder(kDefaultPayloadBufferLength);

  Buffer::OwnedImpl frame_1_part_1;
  Buffer::addSeq(frame_1_part_1, {0x01, 0x03});

  Buffer::OwnedImpl frame_1_part_2;
  Buffer::addSeq(frame_1_part_2, {0x48, 0x65});

  Buffer::OwnedImpl frame_1_part_3_and_frame_2_part_1;
  Buffer::addSeq(frame_1_part_3_and_frame_2_part_1, {0x6c, 0x80, 0x02});

  Buffer::OwnedImpl frame_2_part_2;
  Buffer::addSeq(frame_2_part_2, {0x6c, 0x6f});

  frames = decoder.decode(frame_1_part_1);
  ASSERT_FALSE(frames.has_value());

  frames = decoder.decode(frame_1_part_2);
  ASSERT_FALSE(frames.has_value());

  // frame-1 decoding completes and frame-2 partially decoded and state saved in decoder
  frames = decoder.decode(frame_1_part_3_and_frame_2_part_1);
  ASSERT_TRUE(frames.has_value());

  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0],
                             {false, kFrameOpcodeText, absl::nullopt, 3, makeBuffer("Hel")}));

  // frame-2 completes decoding
  frames = decoder.decode(frame_2_part_2);
  ASSERT_TRUE(frames.has_value());

  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0],
                             {true, kFrameOpcodeContinuation, absl::nullopt, 2, makeBuffer("lo")}));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

// Frame spans over two slices
TEST(WebSocketCodecTest, DecodeFrameSpansOverTwoSlices) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x80, 0x86, 0x7c, 0x96, 0x26, 0x3f, 0x1f, 0xfa});
  Buffer::addSeq(buffer, {0x4f, 0x5a, 0x12, 0xe2});

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0], {true, kFrameOpcodeContinuation, 0x7c96263f, 6,
                                                 makeBuffer("\x1f\xfa\x4f\x5a\x12\xe2")}));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

// A complete frame and an incomplete frame present
TEST(WebSocketCodecTest, DecodeFrameCompleteAndIncompleteFrame) {
  Buffer::OwnedImpl buffer;
  // "hello "
  Buffer::addSeq(buffer, {0x01, 0x86, 0xaf, 0x4b, 0xe8, 0x7a, 0xc7, 0x2e, 0x84, 0x16, 0xc0, 0x6b});
  // " from"
  // with missing payload bytes 0x58, 0x79, 0x51
  Buffer::addSeq(buffer, {0x00, 0x85, 0x3c, 0x33, 0x2a, 0x16, 0x1c, 0x55});
  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_EQ(kFrameOpcodeText, frames.value()[0].opcode_);
  EXPECT_EQ(6, frames.value()[0].payload_length_);
}

// A masked frame spans over two slices split by the length
TEST(WebSocketCodecTest, DecodeFrameSpansOverTwoSlicesSplitByLength) {
  Buffer::OwnedImpl buffer;
  // Length is 126 - represented in the wire by two bytes: 0x00 and 0x7e
  Buffer::addSeq(buffer, {
                             0x82,
                             0xfe,
                             0x00,
                         });
  Buffer::addSeq(buffer, {
                             0x7e, 0xa1, 0xe8, 0xd7, 0xb0, 0x75, 0xe6, 0x2e, 0x56, 0x91, 0x66, 0xb4,
                             0x89, 0x66, 0xa3, 0xd4, 0xaf, 0x15, 0x3e, 0x7d, 0xa0, 0xd6, 0xca, 0xf9,
                             0xc7, 0xd9, 0x5d, 0xc6, 0x33, 0x9d, 0xd5, 0x41, 0x22, 0x0f, 0x18, 0xc1,
                             0xe7, 0xff, 0x8f, 0x2e, 0x6c, 0xaa, 0x81, 0x0f, 0x4e, 0xa5, 0xb6, 0x25,
                             0x4e, 0x8e, 0x3b, 0x25, 0x1e, 0x14, 0x58, 0x12, 0x5f, 0xfc, 0xe8, 0xb8,
                             0xc8, 0xc9, 0xd0, 0xa1, 0x53, 0x04, 0xdb, 0x91, 0xd6, 0x9a, 0xc6, 0x49,
                             0x6d, 0x96, 0x41, 0x8d, 0x9a, 0xd8, 0x00, 0x96, 0x41, 0x8d, 0x54, 0xdf,
                             0x0a, 0x87, 0xea, 0xe7, 0x4c, 0xdf, 0x16, 0x9a, 0x30, 0xa0, 0x26, 0x1c,
                             0xc4, 0x2c, 0xad, 0xfd, 0xd0, 0x2a, 0x16, 0xc5, 0xb4, 0xd7, 0x26, 0xa8,
                             0xca, 0x78, 0xe2, 0x1d, 0xe1, 0x8c, 0xde, 0xa8, 0x35, 0xd7, 0xda, 0x3a,
                             0x07, 0xb2, 0xf7, 0x71, 0x54, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2,
                         });

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  std::vector<uint8_t> payload = {
      0x75, 0xe6, 0x2e, 0x56, 0x91, 0x66, 0xb4, 0x89, 0x66, 0xa3, 0xd4, 0xaf, 0x15, 0x3e,
      0x7d, 0xa0, 0xd6, 0xca, 0xf9, 0xc7, 0xd9, 0x5d, 0xc6, 0x33, 0x9d, 0xd5, 0x41, 0x22,
      0x0f, 0x18, 0xc1, 0xe7, 0xff, 0x8f, 0x2e, 0x6c, 0xaa, 0x81, 0x0f, 0x4e, 0xa5, 0xb6,
      0x25, 0x4e, 0x8e, 0x3b, 0x25, 0x1e, 0x14, 0x58, 0x12, 0x5f, 0xfc, 0xe8, 0xb8, 0xc8,
      0xc9, 0xd0, 0xa1, 0x53, 0x04, 0xdb, 0x91, 0xd6, 0x9a, 0xc6, 0x49, 0x6d, 0x96, 0x41,
      0x8d, 0x9a, 0xd8, 0x00, 0x96, 0x41, 0x8d, 0x54, 0xdf, 0x0a, 0x87, 0xea, 0xe7, 0x4c,
      0xdf, 0x16, 0x9a, 0x30, 0xa0, 0x26, 0x1c, 0xc4, 0x2c, 0xad, 0xfd, 0xd0, 0x2a, 0x16,
      0xc5, 0xb4, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2, 0x1d, 0xe1, 0x8c, 0xde, 0xa8, 0x35,
      0xd7, 0xda, 0x3a, 0x07, 0xb2, 0xf7, 0x71, 0x54, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2,
  };
  EXPECT_TRUE(areFramesEqual(
      frames.value()[0], {true, kFrameOpcodeBinary, 0xa1e8d7b0, 126,
                          std::make_unique<Buffer::OwnedImpl>(payload.data(), payload.size())}));
}

// 1MiB binary message in a single unmasked frame
// 0x82 0xff 0x0000000000100000 [1048576 bytes of binary data]
TEST(WebSocketCodecTest, Decode64BitBinaryMaskedFrame) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(
      buffer, {0x82, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x96, 0x41, 0x8d, 0x54});

  // Add test payload 1048576 bytes
  for (uint32_t i = 0; i < 65536; ++i) {
    Buffer::addSeq(buffer, {0xe7, 0x4c, 0xdf, 0x16, 0x9a, 0x30, 0xa0, 0x26, 0x1c, 0xc4, 0x2c, 0x4e,
                            0x8e, 0x3b, 0x25, 0x1e});
  }

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  std::vector<uint8_t> payload;
  for (uint32_t i = 0; i < 65536; ++i) {
    payload.insert(payload.end(), {0xe7, 0x4c, 0xdf, 0x16, 0x9a, 0x30, 0xa0, 0x26, 0x1c, 0xc4, 0x2c,
                                   0x4e, 0x8e, 0x3b, 0x25, 0x1e});
  }
  EXPECT_TRUE(areFramesEqual(
      frames.value()[0], {true, kFrameOpcodeBinary, 0x96418d54, 1048576,
                          std::make_unique<Buffer::OwnedImpl>(payload.data(), payload.size())}));
}

TEST(WebSocketCodecTest, Decode64BitBinaryMaskedFrameExceptPayload) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(
      buffer, {0x82, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x96, 0x41, 0x8d, 0x54});

  // Add test payload 1048576 bytes
  for (uint32_t i = 0; i < 65536; ++i) {
    Buffer::addSeq(buffer, {0xe7, 0x4c, 0xdf, 0x16, 0x9a, 0x30, 0xa0, 0x26, 0x1c, 0xc4, 0x2c, 0x4e,
                            0x8e, 0x3b, 0x25, 0x1e});
  }

  Decoder decoder(0);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(
      areFramesEqual(frames.value()[0], {true, kFrameOpcodeBinary, 0x96418d54, 1048576, nullptr}));
}

TEST(WebSocketCodecTest, DecodeFramesWithNoPayloadMasked7BitLength) {

  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x81, 0x80, 0x4f, 0x5a, 0x12, 0xe2});

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0], {true, kFrameOpcodeText, 0x4f5a12e2, 0, nullptr}));
}

TEST(WebSocketCodecTest, DecodeFramesWithNoPayloadNonMasked7BitLength) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x81, 0x00});

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(
      areFramesEqual(frames.value()[0], {true, kFrameOpcodeText, absl::nullopt, 0, nullptr}));
}

TEST(WebSocketCodecTest, DecodeFramesWithNoPayloadMasked16BitLength) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x81, 0xfe, 0x00, 0x00, 0x3c, 0x33, 0x2a, 0x16});

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0], {true, kFrameOpcodeText, 0x3c332a16, 0, nullptr}));
}

TEST(WebSocketCodecTest, DecodeFramesWithNoPayloadNonMasked16BitLength) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {
                             0x81,
                             0x7e,
                             0x00,
                             0x00,
                         });

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(
      areFramesEqual(frames.value()[0], {true, kFrameOpcodeText, absl::nullopt, 0, nullptr}));
}

TEST(WebSocketCodecTest, DecodeFramesWithNoPayloadMasked64BitLength) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(
      buffer, {0x81, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xfa, 0x4f, 0x5a});

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0], {true, kFrameOpcodeText, 0x1ffa4f5a, 0, nullptr}));
}

TEST(WebSocketCodecTest, DecodeFramesWithNoPayloadNonMasked64BitLength) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x81, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(
      areFramesEqual(frames.value()[0], {true, kFrameOpcodeText, absl::nullopt, 0, nullptr}));
}

TEST(WebSocketCodecTest, DecodeFramesWhenMaxPayloadBufferLengthSet) {
  Buffer::OwnedImpl buffer;
  // contains "Hel"
  Buffer::addSeq(buffer, {0x01, 0x03, 0x48, 0x65, 0x6c});
  // contains "lo"
  Buffer::addSeq(buffer, {0x80, 0x02, 0x6c, 0x6f});

  Decoder decoder(1);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(2, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0],
                             {false, kFrameOpcodeText, absl::nullopt, 3, makeBuffer("H")}));
  EXPECT_TRUE(areFramesEqual(frames.value()[1],
                             {true, kFrameOpcodeContinuation, absl::nullopt, 2, makeBuffer("l")}));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

TEST(WebSocketCodecTest, Decode16BitFrameWhenMaxPayloadBufferLengthSet) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x82, 0xfe, 0x00, 0x7e, 0xa1, 0xe8, 0xd7, 0xb0});

  std::initializer_list<uint8_t> payload = {
      0x75, 0xe6, 0x2e, 0x56, 0x91, 0x66, 0xb4, 0x89, 0x66, 0xa3, 0xd4, 0xaf, 0x15, 0x3e,
      0x7d, 0xa0, 0xd6, 0xca, 0xf9, 0xc7, 0xd9, 0x5d, 0xc6, 0x33, 0x9d, 0xd5, 0x41, 0x22,
      0x0f, 0x18, 0xc1, 0xe7, 0xff, 0x8f, 0x2e, 0x6c, 0xaa, 0x81, 0x0f, 0x4e, 0xa5, 0xb6,
      0x25, 0x4e, 0x8e, 0x3b, 0x25, 0x1e, 0x14, 0x58, 0x12, 0x5f, 0xfc, 0xe8, 0xb8, 0xc8,
      0xc9, 0xd0, 0xa1, 0x53, 0x04, 0xdb, 0x91, 0xd6, 0x9a, 0xc6, 0x49, 0x6d, 0x96, 0x41,
      0x8d, 0x9a, 0xd8, 0x00, 0x96, 0x41, 0x8d, 0x54, 0xdf, 0x0a, 0x87, 0xea, 0xe7, 0x4c,
      0xdf, 0x16, 0x9a, 0x30, 0xa0, 0x26, 0x1c, 0xc4, 0x2c, 0xad, 0xfd, 0xd0, 0x2a, 0x16,
      0xc5, 0xb4, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2, 0x1d, 0xe1, 0x8c, 0xde, 0xa8, 0x35,
      0xd7, 0xda, 0x3a, 0x07, 0xb2, 0xf7, 0x71, 0x54, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2,
  };
  Buffer::addSeq(buffer, payload);

  // Only 10 bytes length
  Decoder decoder(10);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(
      areFramesEqual(frames.value()[0], {true, kFrameOpcodeBinary, 0xa1e8d7b0, 126,
                                         makeBuffer("\x75\xe6\x2e\x56\x91\x66\xb4\x89\x66\xa3")}));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

TEST(WebSocketCodecTest, Decode64BitFrameWhenMaxPayloadBufferLengthSet) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(
      buffer, {0x82, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x96, 0x41, 0x8d, 0x54});

  // Add test payload 1048576 bytes
  for (uint32_t i = 0; i < 65536; ++i) {
    Buffer::addSeq(buffer, {0xe7, 0x4c, 0xdf, 0x16, 0x9a, 0x30, 0xa0, 0x26, 0x1c, 0xc4, 0x2c, 0x4e,
                            0x8e, 0x3b, 0x25, 0x1e});
  }

  Decoder decoder(20);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(areFramesEqual(
      frames.value()[0],
      {true, kFrameOpcodeBinary, 0x96418d54, 1048576,
       makeBuffer(
           "\xe7\x4c\xdf\x16\x9a\x30\xa0\x26\x1c\xc4\x2c\x4e\x8e\x3b\x25\x1e\xe7\x4c\xdf\x16")}));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

TEST(WebSocketCodecTest, DecodeClosingFrame) {
  Encoder encoder;
  Buffer::OwnedImpl buffer;
  absl::optional<std::vector<uint8_t>> header;

  header = encoder.encodeFrameHeader({false, kFrameOpcodeClose, absl::nullopt, 0, nullptr});
  ASSERT_TRUE(header.has_value());
  buffer.add(header->data(), header->size());

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(
      areFramesEqual(frames.value()[0], {false, kFrameOpcodeClose, absl::nullopt, 0, nullptr}));
}

TEST(WebSocketCodecTest, DecodeInvalidFrame) {
  Buffer::OwnedImpl buffer;
  std::vector<uint8_t> header;
  // Frame header with invalid opcode
  Buffer::addSeq(buffer, {0x0b, 0x03});
  buffer.add("Hey");

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_FALSE(frames.has_value());
}

TEST(WebSocketCodecTest, EncodeAndDecodeFrame) {
  Encoder encoder;
  Buffer::OwnedImpl buffer;
  absl::optional<std::vector<uint8_t>> header;

  header = encoder.encodeFrameHeader({true, kFrameOpcodeText, absl::nullopt, 5, nullptr});
  ASSERT_TRUE(header.has_value());
  buffer.add(header->data(), header->size());
  buffer.add("Hello");

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0],
                             {true, kFrameOpcodeText, absl::nullopt, 5, makeBuffer("Hello")}));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

TEST(WebSocketCodecTest, DecodeMultipleValidNonMaskedFrames) {
  Encoder encoder;
  Buffer::OwnedImpl buffer;
  absl::optional<std::vector<uint8_t>> header = absl::nullopt;

  header = encoder.encodeFrameHeader({false, kFrameOpcodeText, absl::nullopt, 5, nullptr});
  ASSERT_TRUE(header.has_value());
  buffer.add(header->data(), header->size());
  buffer.add("Text ");

  header->clear();
  header = encoder.encodeFrameHeader({true, kFrameOpcodeContinuation, absl::nullopt, 9, nullptr});
  ASSERT_TRUE(header.has_value());
  buffer.add(header->data(), header->size());
  buffer.add("Response!");

  Decoder decoder(kDefaultPayloadBufferLength);
  std::string text_payload;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(2, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0],
                             {false, kFrameOpcodeText, absl::nullopt, 5, makeBuffer("Text ")}));
  EXPECT_TRUE(areFramesEqual(frames.value()[1], {true, kFrameOpcodeContinuation, absl::nullopt, 9,
                                                 makeBuffer("Response!")}));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

TEST(WebSocketCodecTest, DecodeValidMaskedFrame) {
  Encoder encoder;
  Buffer::OwnedImpl buffer;
  absl::optional<std::vector<uint8_t>> header;
  std::vector<uint8_t> payload = {0x7f, 0x9f, 0x4d, 0x51, 0x58};

  header = encoder.encodeFrameHeader({true, kFrameOpcodePong, 0x37fa213d, 5, nullptr});
  ASSERT_TRUE(header.has_value());
  buffer.add(header->data(), header->size());
  buffer.add(payload.data(), payload.size());

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(1, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0], {true, kFrameOpcodePong, 0x37fa213d, 5,
                                                 makeBuffer("\x7f\x9f\x4d\x51\x58")}));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

TEST(WebSocketCodecTest, DecodeValidMultipleMaskedFrames) {
  Encoder encoder;
  Buffer::OwnedImpl buffer;
  absl::optional<std::vector<uint8_t>> header = absl::nullopt;
  std::vector<uint8_t> payload;

  // "hello "
  header = encoder.encodeFrameHeader({false, kFrameOpcodeText, 0xaf4be87a, 6, nullptr});
  ASSERT_TRUE(header.has_value());
  payload = {0xc7, 0x2e, 0x84, 0x16, 0xc0, 0x6b};
  buffer.add(header->data(), header->size());
  buffer.add(payload.data(), payload.size());

  // " from"
  header->clear();
  payload.clear();
  header = encoder.encodeFrameHeader({false, kFrameOpcodeContinuation, 0x3c332a16, 5, nullptr});
  ASSERT_TRUE(header.has_value());
  payload = {0x1c, 0x55, 0x58, 0x79, 0x51};
  buffer.add(header->data(), header->size());
  buffer.add(payload.data(), payload.size());

  // "client"
  header->clear();
  payload.clear();
  header = encoder.encodeFrameHeader({true, kFrameOpcodeContinuation, 0x7c96263f, 6, nullptr});
  ASSERT_TRUE(header.has_value());
  payload = {0x1f, 0xfa, 0x4f, 0x5a, 0x12, 0xe2};
  buffer.add(header->data(), header->size());
  buffer.add(payload.data(), payload.size());

  Decoder decoder(kDefaultPayloadBufferLength);
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  ASSERT_TRUE(frames.has_value());
  EXPECT_EQ(3, frames->size());
  EXPECT_TRUE(areFramesEqual(frames.value()[0], {false, kFrameOpcodeText, 0xaf4be87a, 6,
                                                 makeBuffer("\xc7\x2e\x84\x16\xc0\x6b")}));
  EXPECT_TRUE(areFramesEqual(frames.value()[1], {false, kFrameOpcodeContinuation, 0x3c332a16, 5,
                                                 makeBuffer("\x1c\x55\x58\x79\x51")}));
  EXPECT_TRUE(areFramesEqual(frames.value()[2], {true, kFrameOpcodeContinuation, 0x7c96263f, 6,
                                                 makeBuffer("\x1f\xfa\x4f\x5a\x12\xe2")}));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

} // namespace
} // namespace WebSocket
} // namespace Envoy
