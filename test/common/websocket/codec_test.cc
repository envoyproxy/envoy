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

// A single-frame unmasked text message
// 0x81 0x05 0x48 0x65 0x6c 0x6c 0x6f (contains "Hello")
//                   H        e        l        l        o
// 10000001 00000101 01001000 01100101 01101100 01101100 01101111
// fin - 1
// opcode - 0x1 - text data
// mask bit - 0
// length - 5 bytes 0000101
TEST(WebSocketCodecTest, decodeSingleUnmaskedFrame) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f});

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(false, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(1, frames.value().size());
  EXPECT_EQ(5, frames.value()[0].payload_length_);

  std::string text_payload;
  text_payload.resize(5);
  (*(frames.value()[0].payload_)).copyOut(0, 5, text_payload.data());
  EXPECT_EQ("Hello", text_payload);
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
TEST(WebSocketCodecTest, decodeFragmentedUnmaskedTextMessage) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x01, 0x03, 0x48, 0x65, 0x6c});
  Buffer::addSeq(buffer, {0x80, 0x02, 0x6c, 0x6f});

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(2, frames.value().size());

  EXPECT_EQ(false, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(3, frames.value()[0].payload_length_);

  std::string text_payload_first_frame;
  text_payload_first_frame.resize(3);
  (*(frames.value()[0].payload_)).copyOut(0, 3, text_payload_first_frame.data());
  EXPECT_EQ("Hel", text_payload_first_frame);

  EXPECT_EQ(false, frames.value()[1].masking_key_.has_value());
  EXPECT_EQ(2, frames.value()[1].payload_length_);

  std::string text_payload_second_frame;
  text_payload_second_frame.resize(2);
  (*(frames.value()[1].payload_)).copyOut(0, 2, text_payload_second_frame.data());
  EXPECT_EQ("lo", text_payload_second_frame);
}

// A fragmented and unfragmented text message
TEST(WebSocketCodecTest, decodeFragmentedAndUnfragmentedTextMessages) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x01, 0x03, 0x48, 0x65, 0x6c});
  Buffer::addSeq(buffer, {0x80, 0x02, 0x6c, 0x6f});
  Buffer::addSeq(buffer, {0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f});

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(3, frames.value().size());

  EXPECT_EQ(false, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(3, frames.value()[0].payload_length_);

  std::string text_payload_first_frame;
  text_payload_first_frame.resize(3);
  (*(frames.value()[0].payload_)).copyOut(0, 3, text_payload_first_frame.data());
  EXPECT_EQ("Hel", text_payload_first_frame);

  EXPECT_EQ(false, frames.value()[1].masking_key_.has_value());
  EXPECT_EQ(2, frames.value()[1].payload_length_);

  std::string text_payload_second_frame;
  text_payload_second_frame.resize(2);
  (*(frames.value()[1].payload_)).copyOut(0, 2, text_payload_second_frame.data());
  EXPECT_EQ("lo", text_payload_second_frame);

  EXPECT_EQ(false, frames.value()[2].masking_key_.has_value());
  EXPECT_EQ(5, frames.value()[2].payload_length_);

  std::string text_payload_third_frame;
  text_payload_third_frame.resize(5);
  (*(frames.value()[2].payload_)).copyOut(0, 5, text_payload_third_frame.data());
  EXPECT_EQ("Hello", text_payload_third_frame);
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
TEST(WebSocketCodecTest, decodeSingleMaskedFrame) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58});

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(1, frames.value().size());
  EXPECT_EQ(true, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(5, frames.value()[0].payload_length_);
  EXPECT_EQ(0x37fa213d, frames.value()[0].masking_key_.value());

  std::string text_payload;
  text_payload.resize(5);
  (*(frames.value()[0].payload_)).copyOut(0, 5, text_payload.data());
  // Unmasking the text payload
  for (size_t i = 0; i < text_payload.size(); ++i) {
    text_payload[i] ^= (frames.value()[0].masking_key_.value() >> (8 * (3 - i % 4))) & 0xff;
  }
  EXPECT_EQ("Hello", text_payload);
}

// Unmasked Ping request and masked Ping response
//
// 0x89 0x05 0x48 0x65 0x6c 0x6c 0x6f (contains a body of "Hello",
// but the contents of the body are arbitrary)
//
// 0x8a 0x85 0x37 0xfa 0x21 0x3d 0x7f 0x9f 0x4d 0x51 0x58
// (contains a body of "Hello", matching the body of the ping)
TEST(WebSocketCodecTest, decodePingFrames) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x89, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f});
  Buffer::addSeq(buffer, {0x8a, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58});

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(2, frames.value().size());
  EXPECT_EQ(false, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(5, frames.value()[0].payload_length_);

  std::string ping_request_payload;
  ping_request_payload.resize(5);
  (*(frames.value()[0].payload_)).copyOut(0, 5, ping_request_payload.data());
  EXPECT_EQ("Hello", ping_request_payload);

  EXPECT_EQ(true, frames.value()[1].masking_key_.has_value());
  EXPECT_EQ(5, frames.value()[1].payload_length_);
  EXPECT_EQ(0x37fa213d, frames.value()[1].masking_key_.value());

  std::string ping_response_payload;
  ping_response_payload.resize(5);
  (*(frames.value()[1].payload_)).copyOut(0, 5, ping_response_payload.data());
  // Unmasking the ping response payload
  for (size_t i = 0; i < ping_response_payload.size(); ++i) {
    ping_response_payload[i] ^=
        (frames.value()[1].masking_key_.value() >> (8 * (3 - i % 4))) & 0xff;
  }
  EXPECT_EQ("Hello", ping_response_payload);
}

// 256 bytes binary message in a single unmasked frame
// 0x82 0x7E 0x0100 [256 bytes of binary data]
TEST(WebSocketCodecTest, decode16BitBinaryUnmaskedFrame) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer,
                 {0x82, 0x7e, 0x01, 0x00, 0xd9, 0xac, 0x96, 0x06, 0xc2, 0x83, 0x41, 0x31, 0x16,
                  0x6c, 0x27, 0x6e, 0x3c, 0xc9, 0x6b, 0x7f, 0x12, 0x32, 0xb4, 0x3f, 0x19, 0x9a,
                  0x0a, 0x23, 0x6e, 0xfb, 0xb0, 0xda, 0x40, 0x4a, 0xd2, 0xf4, 0xc0, 0xee, 0xa3,
                  0xc2, 0x3e, 0x7c, 0x96, 0x26, 0x3f, 0x14, 0x34, 0xee, 0xfe, 0xa5, 0x42, 0x23,
                  0x5b, 0xc8, 0x43, 0xe4, 0xd4, 0xf7, 0xde, 0x79, 0xa6, 0xf9, 0x40, 0xfa, 0xfe,
                  0xb0, 0x8e, 0xe7, 0x26, 0xa9, 0xa3, 0x6f, 0x07, 0x5c, 0x0a});
  Buffer::addSeq(buffer,
                 {0xf1, 0x78, 0xc9, 0xb7, 0x34, 0x80, 0xd9, 0x22, 0xdd, 0x68, 0x0a, 0x34, 0xba,
                  0x84, 0x64, 0x4d, 0x62, 0x98, 0x44, 0xce, 0x92, 0x0c, 0x16, 0xb1, 0xa5, 0x9b,
                  0x23, 0xb2, 0xf5, 0x6a, 0x99, 0xc7, 0x9f, 0x89, 0xaf, 0x4b, 0xe8, 0x7a, 0x5e,
                  0x7d, 0xf5, 0xe6, 0x6a, 0x3e, 0x4a, 0xc9, 0x5e, 0x70, 0xbf, 0x04, 0x3c, 0x33,
                  0x2a, 0x16, 0xc5, 0xb4, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2, 0x1d, 0xe1, 0xb1,
                  0x8c, 0xde, 0xa8, 0x35, 0xd7, 0xda, 0x3a, 0x07, 0xb2, 0xf7, 0x71, 0x54, 0x31,
                  0x7a, 0x1f, 0x45, 0x1c, 0xf9, 0x82, 0xd5, 0x47, 0xfe, 0xc7, 0xb7, 0xd0});

  Buffer::addSeq(buffer, {0x75, 0xe6, 0x2e, 0x56, 0x91, 0x66, 0xb4, 0x89, 0x66, 0xa3, 0xd4, 0xaf,
                          0x15, 0x3e, 0x7d, 0xa0, 0xd6, 0xca, 0xf9, 0xc7, 0xd9, 0x5d, 0xc6, 0x33,
                          0x9d, 0xd5, 0x41, 0x22, 0x0f, 0x18, 0xc1, 0xe7, 0xff, 0x8f, 0x2e, 0x6c,
                          0xaa, 0x81, 0x0f, 0x4e, 0xa5, 0xb6, 0x25, 0x4e, 0x8e, 0x3b, 0x25, 0x1e,
                          0x14, 0x58, 0x12, 0x5f, 0xfc, 0xe8, 0xb8, 0xc8, 0xc9, 0xd0, 0xa1, 0x53,
                          0x04, 0xdb, 0x91, 0xd6, 0x9a, 0xc6, 0xa1, 0xe8, 0xd7, 0xb0, 0x49, 0x6d,
                          0xd8, 0x00, 0x96, 0x41, 0x8d, 0x54, 0xdf, 0x0a, 0x87, 0xea, 0xe7, 0x4c,
                          0xdf, 0x16, 0x9a, 0x30, 0xa0, 0x26, 0x1c, 0xc4, 0x2c, 0xad, 0xfd});

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(1, frames.value().size());
  EXPECT_EQ(2, frames.value()[0].opcode_);
  EXPECT_TRUE(frames.value()[0].final_fragment_);
  EXPECT_EQ(256, frames.value()[0].payload_length_);
  EXPECT_EQ(false, frames.value()[0].masking_key_.has_value());
}

// 126 bytes binary message in a single masked frame
TEST(WebSocketCodecTest, decode16BitBinaryMaskedFrame) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(
      buffer,
      {
          0x82, 0xfe, 0x00, 0x7e, 0xa1, 0xe8, 0xd7, 0xb0, 0x75, 0xe6, 0x2e, 0x56, 0x91, 0x66, 0xb4,
          0x89, 0x66, 0xa3, 0xd4, 0xaf, 0x15, 0x3e, 0x7d, 0xa0, 0xd6, 0xca, 0xf9, 0xc7, 0xd9, 0x5d,
          0xc6, 0x33, 0x9d, 0xd5, 0x41, 0x22, 0x0f, 0x18, 0xc1, 0xe7, 0xff, 0x8f, 0x2e, 0x6c, 0xaa,
          0x81, 0x0f, 0x4e, 0xa5, 0xb6, 0x25, 0x4e, 0x8e, 0x3b, 0x25, 0x1e, 0x14, 0x58, 0x12, 0x5f,
          0xfc, 0xe8, 0xb8, 0xc8, 0xc9, 0xd0, 0xa1, 0x53, 0x04, 0xdb, 0x91, 0xd6, 0x9a, 0xc6, 0x49,
          0x6d, 0x96, 0x41, 0x8d, 0x9a, 0xd8, 0x00, 0x96, 0x41, 0x8d, 0x54, 0xdf, 0x0a, 0x87, 0xea,
          0xe7, 0x4c, 0xdf, 0x16, 0x9a, 0x30, 0xa0, 0x26, 0x1c, 0xc4, 0x2c, 0xad, 0xfd, 0xd0, 0x2a,
          0x16, 0xc5, 0xb4, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2, 0x1d, 0xe1, 0x8c, 0xde, 0xa8, 0x35,
          0xd7, 0xda, 0x3a, 0x07, 0xb2, 0xf7, 0x71, 0x54, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2,
      });
  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(1, frames.value().size());
  EXPECT_EQ(2, frames.value()[0].opcode_);
  EXPECT_TRUE(frames.value()[0].final_fragment_);
  EXPECT_EQ(126, frames.value()[0].payload_length_);
  EXPECT_EQ(true, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(0xa1e8d7b0, frames.value()[0].masking_key_.value());
}

// 64KiB binary message in a single unmasked frame
// 0x82 0x7f 0x0000000000010000 [65536 bytes of binary data]
TEST(WebSocketCodecTest, decode64BitBinaryUnmaskedFrame) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x82, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00});

  // Add test payload 65536 bytes
  for (uint16_t i = 0; i < 4096; ++i) {
    Buffer::addSeq(buffer, {0x16, 0xc5, 0xb4, 0xd7, 0x26, 0xa8, 0xca, 0x78, 0xe2, 0x1d, 0xe1, 0x8c,
                            0xde, 0xa8, 0x35, 0xde});
  }

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(2, frames.value()[0].opcode_);
  EXPECT_TRUE(frames.value()[0].final_fragment_);
  EXPECT_EQ(65536, frames.value()[0].payload_length_);
  EXPECT_EQ(false, frames.value()[0].masking_key_.has_value());
}

// 1MiB binary message in a single unmasked frame
// 0x82 0xff 0x0000000000100000 [1048576 bytes of binary data]
TEST(WebSocketCodecTest, decode64BitBinaryMaskedFrame) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(
      buffer, {0x82, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x96, 0x41, 0x8d, 0x54});

  // Add test payload 1048576 bytes
  for (uint32_t i = 0; i < 65536; ++i) {
    Buffer::addSeq(buffer, {0xe7, 0x4c, 0xdf, 0x16, 0x9a, 0x30, 0xa0, 0x26, 0x1c, 0xc4, 0x2c, 0x4e,
                            0x8e, 0x3b, 0x25, 0x1e});
  }

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(2, frames.value()[0].opcode_);
  EXPECT_TRUE(frames.value()[0].final_fragment_);
  EXPECT_EQ(1048576, frames.value()[0].payload_length_);
  EXPECT_EQ(true, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(0x96418d54, frames.value()[0].masking_key_.value());
}

TEST(WebSocketCodecTest, decodeFramesWithNoPayloadMasked7BitLength) {

  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x81, 0x80, 0x4f, 0x5a, 0x12, 0xe2});

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(true, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(0x4f5a12e2, frames.value()[0].masking_key_.value());
  EXPECT_EQ(1, frames.value().size());
  EXPECT_EQ(0, frames.value()[0].payload_length_);
}

TEST(WebSocketCodecTest, decodeFramesWithNoPayloadNonMasked7BitLength) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x81, 0x00});

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(false, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(1, frames.value().size());
  EXPECT_EQ(0, frames.value()[0].payload_length_);
}

TEST(WebSocketCodecTest, decodeFramesWithNoPayloadMasked16BitLength) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x81, 0xfe, 0x00, 0x00, 0x3c, 0x33, 0x2a, 0x16});

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(1, frames.value().size());
  EXPECT_EQ(true, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(0x3c332a16, frames.value()[0].masking_key_.value());
  EXPECT_EQ(1, frames.value().size());
  EXPECT_EQ(0, frames.value()[0].payload_length_);
}

TEST(WebSocketCodecTest, decodeFramesWithNoPayloadNonMasked16BitLength) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {
                             0x81,
                             0x7e,
                             0x00,
                             0x00,
                         });

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(false, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(1, frames.value().size());
  EXPECT_EQ(0, frames.value()[0].payload_length_);
}

TEST(WebSocketCodecTest, decodeFramesWithNoPayloadMasked64BitLength) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(
      buffer, {0x81, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xfa, 0x4f, 0x5a});

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(true, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(0x1ffa4f5a, frames.value()[0].masking_key_.value());
  EXPECT_EQ(1, frames.value().size());
  EXPECT_EQ(0, frames.value()[0].payload_length_);
}

TEST(WebSocketCodecTest, decodeFramesWithNoPayloadNonMasked64BitLength) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x81, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(false, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(1, frames.value().size());
  EXPECT_EQ(0, frames.value()[0].payload_length_);
}

TEST(WebSocketCodecTest, encodeFrameHeader) {
  Encoder encoder;
  std::vector<uint8_t> buffer;
  std::vector<uint8_t> expected_header;

  buffer.clear();
  buffer = encoder.encodeFrameHeader({true, 1, absl::nullopt, 5, nullptr});
  EXPECT_EQ(buffer.size(), 2);
  expected_header = {0x81, 0x05};
  EXPECT_EQ(buffer, expected_header);

  buffer.clear();
  buffer = encoder.encodeFrameHeader({true, 1, 0x37fa213d, 5, nullptr});
  EXPECT_EQ(buffer.size(), 6);
  expected_header = {0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d};
  EXPECT_EQ(buffer, expected_header);

  buffer.clear();
  buffer = encoder.encodeFrameHeader({false, 0, 0x3c332a16, 5, nullptr});
  EXPECT_EQ(buffer.size(), 6);
  expected_header = {0x00, 0x85, 0x3c, 0x33, 0x2a, 0x16};
  EXPECT_EQ(buffer, expected_header);

  buffer.clear();
  buffer = encoder.encodeFrameHeader({true, 2, absl::nullopt, 256, nullptr});
  EXPECT_EQ(buffer.size(), 4);
  expected_header = {0x82, 0x7e, 0x01, 0x00};
  EXPECT_EQ(buffer, expected_header);

  buffer.clear();
  buffer = encoder.encodeFrameHeader({true, 2, 0x37fa213d, 256, nullptr});
  EXPECT_EQ(buffer.size(), 8);
  expected_header = {0x82, 0xfe, 0x01, 0x00, 0x37, 0xfa, 0x21, 0x3d};
  EXPECT_EQ(buffer, expected_header);

  buffer.clear();
  buffer = encoder.encodeFrameHeader({true, 2, absl::nullopt, 77777, nullptr});
  EXPECT_EQ(buffer.size(), 10);
  expected_header = {0x82, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x2f, 0xd1};
  EXPECT_EQ(buffer, expected_header);

  buffer.clear();
  buffer = encoder.encodeFrameHeader({true, 2, 0x37fa213d, 77777, nullptr});
  EXPECT_EQ(buffer.size(), 14);
  expected_header = {0x82, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x01, 0x2f, 0xd1, 0x37, 0xfa, 0x21, 0x3d};
  EXPECT_EQ(buffer, expected_header);
}

TEST(GrpcCodecTest, decodeClosingFrame) {
  Encoder encoder;
  Buffer::OwnedImpl buffer;
  std::vector<uint8_t> header;

  header = encoder.encodeFrameHeader({false, 8, absl::nullopt, 0, nullptr});
  buffer.add(header.data(), header.size());

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(1, frames.value().size());
  EXPECT_EQ(0, frames.value()[0].payload_length_);
  EXPECT_EQ(8, frames.value()[0].opcode_);
  EXPECT_FALSE(frames.value()[0].final_fragment_);
}

TEST(GrpcCodecTest, decodeInvalidFrame) {
  Encoder encoder;
  Buffer::OwnedImpl buffer;
  std::vector<uint8_t> header;
  // Invalid opcode
  header = encoder.encodeFrameHeader({false, 0x0b, absl::nullopt, 3, nullptr});
  buffer.add(header.data(), header.size());
  buffer.add("Hey");

  Decoder decoder;
  size_t size = buffer.length();
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(frames, absl::nullopt);
  EXPECT_EQ(size, buffer.length());
}

TEST(WebSocketCodecTest, encodeAndDecodeFrame) {
  Encoder encoder;
  Buffer::OwnedImpl buffer;
  std::vector<uint8_t> header;

  header = encoder.encodeFrameHeader({true, 1, absl::nullopt, 5, nullptr});
  buffer.add(header.data(), header.size());
  buffer.add("Hello");

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(false, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(1, frames.value().size());
  EXPECT_EQ(5, frames.value()[0].payload_length_);

  std::string text_payload;
  text_payload.resize(5);
  (*(frames.value()[0].payload_)).copyOut(0, 5, text_payload.data());
  EXPECT_EQ("Hello", text_payload);
}

TEST(WebSocketCodecTest, decodeMultipleValidNonMaskedFrames) {
  Encoder encoder;
  Buffer::OwnedImpl buffer;
  std::vector<uint8_t> header;

  header = encoder.encodeFrameHeader({false, 1, absl::nullopt, 5, nullptr});
  buffer.add(header.data(), header.size());
  buffer.add("Text ");

  header.clear();
  header = encoder.encodeFrameHeader({true, 0, absl::nullopt, 9, nullptr});
  buffer.add(header.data(), header.size());
  buffer.add("Response!");

  Decoder decoder;
  std::string text_payload;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(2, frames.value().size());

  EXPECT_EQ(false, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(5, frames.value()[0].payload_length_);
  text_payload.resize(5);
  (*(frames.value()[0].payload_)).copyOut(0, 5, text_payload.data());
  EXPECT_EQ("Text ", text_payload);

  EXPECT_EQ(false, frames.value()[1].masking_key_.has_value());
  EXPECT_EQ(9, frames.value()[1].payload_length_);
  text_payload.resize(9);
  (*(frames.value()[1].payload_)).copyOut(0, 9, text_payload.data());
  EXPECT_EQ("Response!", text_payload);
}

TEST(WebSocketCodecTest, decodeValidMaskedFrame) {
  Encoder encoder;
  Buffer::OwnedImpl buffer;
  std::vector<uint8_t> header;
  std::vector<uint8_t> payload = {0x7f, 0x9f, 0x4d, 0x51, 0x58};

  header = encoder.encodeFrameHeader({true, 0x0a, 0x37fa213d, 5, nullptr});
  buffer.add(header.data(), header.size());
  buffer.add(payload.data(), payload.size());

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(1, frames.value().size());
  EXPECT_EQ(true, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(0x37fa213d, frames.value()[0].masking_key_.value());
  EXPECT_EQ(5, frames.value()[0].payload_length_);

  std::string text_payload;
  text_payload.resize(5);
  (*(frames.value()[0].payload_)).copyOut(0, 5, text_payload.data());
  // Unmasking the text payload
  for (size_t i = 0; i < text_payload.size(); ++i) {
    text_payload[i] ^= (frames.value()[0].masking_key_.value() >> (8 * (3 - i % 4))) & 0xff;
  }
  EXPECT_EQ("Hello", text_payload);
}

TEST(WebSocketCodecTest, decodeValidMultipleMaskedFrames) {
  Encoder encoder;
  Buffer::OwnedImpl buffer;
  std::vector<uint8_t> header;
  std::vector<uint8_t> payload;

  // "hello "
  header = encoder.encodeFrameHeader({false, 1, 0xaf4be87a, 6, nullptr});
  payload = {0xc7, 0x2e, 0x84, 0x16, 0xc0, 0x6b};
  buffer.add(header.data(), header.size());
  buffer.add(payload.data(), payload.size());

  // " from"
  header.clear();
  payload.clear();
  header = encoder.encodeFrameHeader({false, 0, 0x3c332a16, 5, nullptr});
  payload = {0x1c, 0x55, 0x58, 0x79, 0x51};
  buffer.add(header.data(), header.size());
  buffer.add(payload.data(), payload.size());

  // "client"
  header.clear();
  payload.clear();
  header = encoder.encodeFrameHeader({true, 0, 0x7c96263f, 6, nullptr});
  payload = {0x1f, 0xfa, 0x4f, 0x5a, 0x12, 0xe2};
  buffer.add(header.data(), header.size());
  buffer.add(payload.data(), payload.size());

  Decoder decoder;
  absl::optional<std::vector<Frame>> frames = decoder.decode(buffer);

  EXPECT_EQ(3, frames.value().size());

  EXPECT_EQ(true, frames.value()[0].masking_key_.has_value());
  EXPECT_EQ(0xaf4be87a, frames.value()[0].masking_key_.value());
  EXPECT_EQ(6, frames.value()[0].payload_length_);

  EXPECT_EQ(true, frames.value()[1].masking_key_.has_value());
  EXPECT_EQ(0x3c332a16, frames.value()[1].masking_key_.value());
  EXPECT_EQ(5, frames.value()[1].payload_length_);

  EXPECT_EQ(true, frames.value()[2].masking_key_.has_value());
  EXPECT_EQ(0x7c96263f, frames.value()[2].masking_key_.value());
  EXPECT_EQ(6, frames.value()[2].payload_length_);

  std::string text_payload;

  text_payload.resize(6);
  (*(frames.value()[0].payload_)).copyOut(0, 6, text_payload.data());
  // Unmasking the text payload
  for (size_t i = 0; i < text_payload.size(); ++i) {
    text_payload[i] ^= (frames.value()[0].masking_key_.value() >> (8 * (3 - i % 4))) & 0xff;
  }
  EXPECT_EQ("hello ", text_payload);

  text_payload.resize(5);
  (*(frames.value()[1].payload_)).copyOut(0, 5, text_payload.data());
  // Unmasking the text payload
  for (size_t i = 0; i < text_payload.size(); ++i) {
    text_payload[i] ^= (frames.value()[1].masking_key_.value() >> (8 * (3 - i % 4))) & 0xff;
  }
  EXPECT_EQ(" from", text_payload);

  text_payload.resize(6);
  (*(frames.value()[2].payload_)).copyOut(0, 6, text_payload.data());
  // Unmasking the text payload
  for (size_t i = 0; i < text_payload.size(); ++i) {
    text_payload[i] ^= (frames.value()[2].masking_key_.value() >> (8 * (3 - i % 4))) & 0xff;
  }
  EXPECT_EQ("client", text_payload);
}

} // namespace
} // namespace WebSocket
} // namespace Envoy
