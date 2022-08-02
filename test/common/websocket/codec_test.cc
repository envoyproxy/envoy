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
  std::vector<Frame> frames;
  EXPECT_TRUE(decoder.decode(buffer, frames));

  EXPECT_EQ(false, frames[0].is_masked_);
  EXPECT_EQ(1, frames.size());
  EXPECT_EQ(5, frames[0].payload_length_);

  std::string text_payload;
  text_payload.resize(5);
  (*(frames[0].payload_)).copyOut(0, 5, text_payload.data());
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
  std::vector<Frame> frames;
  EXPECT_TRUE(decoder.decode(buffer, frames));

  EXPECT_EQ(2, frames.size());

  EXPECT_EQ(false, frames[0].is_masked_);
  EXPECT_EQ(3, frames[0].payload_length_);

  std::string text_payload_first_frame;
  text_payload_first_frame.resize(3);
  (*(frames[0].payload_)).copyOut(0, 3, text_payload_first_frame.data());
  EXPECT_EQ("Hel", text_payload_first_frame);

  EXPECT_EQ(false, frames[1].is_masked_);
  EXPECT_EQ(2, frames[1].payload_length_);

  std::string text_payload_second_frame;
  text_payload_second_frame.resize(2);
  (*(frames[1].payload_)).copyOut(0, 2, text_payload_second_frame.data());
  EXPECT_EQ("lo", text_payload_second_frame);
}

// A single-frame masked text message ("Hello")
// 0x81 0x85 0x37 0xfa 0x21 0x3d 0x7f 0x9f 0x4d 0x51 0x58
//           M1   M2   M3   M4   B1   B2   B3   B4   B5
// fin - 1
// opcode - 0x1 - text data
// mask bit - 1
// length - 5 bytes 0000101
//
// H - M1 XOR B1      01001000 = 00110111 xor 01111111
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
  std::vector<Frame> frames;
  EXPECT_TRUE(decoder.decode(buffer, frames));

  EXPECT_EQ(1, frames.size());
  EXPECT_EQ(true, frames[0].is_masked_);
  EXPECT_EQ(5, frames[0].payload_length_);
  EXPECT_EQ(0x37fa213d, frames[0].masking_key_);

  std::string text_payload;
  text_payload.resize(5);
  (*(frames[0].payload_)).copyOut(0, 5, text_payload.data());
  // Unmasking the text paylaod
  for (size_t i = 0; i < text_payload.size(); ++i) {
    text_payload[i] ^= (frames[0].masking_key_ >> (8 * (3 - i % 4))) & 0xff;
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
  std::vector<Frame> frames;
  EXPECT_TRUE(decoder.decode(buffer, frames));

  EXPECT_EQ(2, frames.size());
  EXPECT_EQ(false, frames[0].is_masked_);
  EXPECT_EQ(5, frames[0].payload_length_);

  std::string ping_request_payload;
  ping_request_payload.resize(5);
  (*(frames[0].payload_)).copyOut(0, 5, ping_request_payload.data());
  EXPECT_EQ("Hello", ping_request_payload);

  EXPECT_EQ(true, frames[1].is_masked_);
  EXPECT_EQ(5, frames[1].payload_length_);
  EXPECT_EQ(0x37fa213d, frames[1].masking_key_);

  std::string ping_response_payload;
  ping_response_payload.resize(5);
  (*(frames[1].payload_)).copyOut(0, 5, ping_response_payload.data());
  // Unmasking the ping response paylaod
  for (size_t i = 0; i < ping_response_payload.size(); ++i) {
    ping_response_payload[i] ^= (frames[1].masking_key_ >> (8 * (3 - i % 4))) & 0xff;
  }
  EXPECT_EQ("Hello", ping_response_payload);
}

// 256 bytes binary message in a single unmasked frame
// 0x82 0x7E 0x0100 [256 bytes of binary data]
TEST(WebSocketCodecTest, decode16BitBinaryFrame) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x82, 0x7E, 0x01, 0x00});

  Decoder decoder;
  std::vector<Frame> frames;
  EXPECT_TRUE(decoder.decode(buffer, frames));

  EXPECT_EQ(0x82, decoder.getFrame().flags_and_opcode_);
  EXPECT_EQ(256, decoder.getFrame().payload_length_);
  EXPECT_EQ(false, decoder.getFrame().is_masked_);
  EXPECT_EQ(decoder.state(), State::Payload);
}

// 64KiB binary message in a single unmasked frame
// 0x82 0x7F 0x0000000000010000 [65536 bytes of binary data]
TEST(WebSocketCodecTest, decode64BitBinaryFrame) {
  Buffer::OwnedImpl buffer;
  Buffer::addSeq(buffer, {0x82, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00});

  Decoder decoder;
  std::vector<Frame> frames;
  EXPECT_TRUE(decoder.decode(buffer, frames));

  EXPECT_EQ(0x82, decoder.getFrame().flags_and_opcode_);
  EXPECT_EQ(65536, decoder.getFrame().payload_length_);
  EXPECT_EQ(false, decoder.getFrame().is_masked_);
  EXPECT_EQ(decoder.state(), State::Payload);
}

TEST(WebSocketCodecTest, FrameInspectorTest) {
  {
    Buffer::OwnedImpl buffer;
    FrameInspector counter;
    EXPECT_EQ(0, counter.inspect(buffer));
    EXPECT_EQ(counter.state(), State::FhFlagsAndOpcode);
    EXPECT_EQ(counter.frameCount(), 0);
  }

  {
    Buffer::OwnedImpl buffer;
    FrameInspector counter;
    Buffer::addSeq(buffer, {0x81});
    EXPECT_EQ(1, counter.inspect(buffer));
    EXPECT_EQ(counter.state(), State::FhMaskFlagAndLength);
    EXPECT_EQ(counter.frameCount(), 1);
  }
}

} // namespace
} // namespace WebSocket
} // namespace Envoy
