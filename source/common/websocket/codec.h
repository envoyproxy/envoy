#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "envoy/buffer/buffer.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace WebSocket {

// Opcodes (https://datatracker.ietf.org/doc/html/rfc6455#section-11.8)
constexpr uint8_t kFrameOpcodeContinuation = 0;
constexpr uint8_t kFrameOpcodeText = 1;
constexpr uint8_t kFrameOpcodeBinary = 2;
constexpr uint8_t kFrameOpcodeClose = 8;
constexpr uint8_t kFrameOpcodePing = 9;
constexpr uint8_t kFrameOpcodePong = 10;
constexpr std::array<uint8_t, 6> kFrameOpcodes = {kFrameOpcodeContinuation, kFrameOpcodeText,
                                                  kFrameOpcodeBinary,       kFrameOpcodeClose,
                                                  kFrameOpcodePing,         kFrameOpcodePong};

// Length of the masking key which is 4 bytes fixed size
constexpr uint8_t kMaskingKeyLength = 4;
// 16 bit payload length
constexpr uint8_t kPayloadLength16Bit = 2;
// 64 bit payload length
constexpr uint8_t kPayloadLength64Bit = 8;
// Maximum payload buffer length
constexpr uint64_t kMaxPayloadBufferLength = 0x7fffffffffffffff;

// Wire format (https://datatracker.ietf.org/doc/html/rfc6455#section-5.2)
// of WebSocket frame:
//
//   0                   1                   2                   3
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//  +-+-+-+-+-------+-+-------------+-------------------------------+
//  |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
//  |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
//  |N|V|V|V|       |S|             |   (if payload len==126/127)   |
//  | |1|2|3|       |K|             |                               |
//  +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
//  |     Extended payload length continued, if payload len == 127  |
//  + - - - - - - - - - - - - - - - +-------------------------------+
//  |                               | Masking-key, if MASK set to 1 |
//  +-------------------------------+-------------------------------+
//  | Masking-key (continued)       |          Payload Data         |
//  +-------------------------------- - - - - - - - - - - - - - - - +
//  : .... Payload Data continued .... Payload Data continued ..... :
//  + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
//  | .... Payload Data continued .... Payload Data continued ..... |
//  +---------------------------------------------------------------+

// In-memory representation of the contents of a WebSocket frame.
struct Frame {
  // Indicates that this is the final fragment in a message.
  bool final_fragment_;
  // Frame opcode.
  uint8_t opcode_;
  // The 4 byte fixed size masking key used to mask the payload. Masking/unmasking should be
  // performed as described in https://datatracker.ietf.org/doc/html/rfc6455#section-5.3
  absl::optional<uint32_t> masking_key_;
  // Length of the payload as the number of bytes.
  uint64_t payload_length_;
  // WebSocket payload data (extension data and application data).
  Buffer::InstancePtr payload_;
};

// Encoder encodes in memory WebSocket frames into frames in the wire format
class Encoder : public Logger::Loggable<Logger::Id::websocket> {
public:
  Encoder() = default;

  // Creates a new Websocket data frame header with the given frame data.
  // @param frame supplies the frame to be encoded.
  // @return std::vector<uint8_t> buffer with encoded header data.
  absl::optional<std::vector<uint8_t>> encodeFrameHeader(const Frame& frame);
};

// Decoder decodes bytes in input buffer into in-memory WebSocket frames.
class Decoder : public Logger::Loggable<Logger::Id::websocket> {
public:
  Decoder(uint64_t max_payload_length = 0)
      : max_payload_buffer_length_{std::min(max_payload_length, kMaxPayloadBufferLength)} {};
  // Decodes the given buffer into WebSocket frames. If the input is not sufficient to make a
  // complete WebSocket frame, then the decoder saves the state of halfway decoded WebSocket
  // frame until the next decode calls feed rest of the frame data.
  // @param input supplies the binary octets wrapped in a WebSocket frame.
  // @return the decoded frames.
  absl::optional<std::vector<Frame>> decode(const Buffer::Instance& input);

private:
  void resetDecoder();
  void frameDataStart();
  void frameData(const uint8_t* mem, uint64_t length);
  void frameDataEnd(std::vector<Frame>& output);

  uint8_t doDecodeFlagsAndOpcode(absl::Span<const uint8_t>& data);
  uint8_t doDecodeMaskFlagAndLength(absl::Span<const uint8_t>& data);
  uint8_t doDecodeExtendedLength(absl::Span<const uint8_t>& data);
  uint8_t doDecodeMaskingKey(absl::Span<const uint8_t>& data);
  uint64_t doDecodePayload(absl::Span<const uint8_t>& data);

  // Current state of the frame that is being processed.
  enum class State {
    // Decoding the first byte. Waiting for decoding the final frame flag (1 bit)
    // and reserved flags (3 bits) and opcode (4 bits) of the WebSocket data frame.
    FrameHeaderFlagsAndOpcode,
    // Decoding the second byte. Waiting for decoding the mask flag (1 bit) and
    // length/length flag (7 bit) of the WebSocket data frame.
    FrameHeaderMaskFlagAndLength,
    // Waiting for decoding the extended 16 bit length.
    FrameHeaderExtendedLength16Bit,
    // Waiting for decoding the extended 64 bit length.
    FrameHeaderExtendedLength64Bit,
    // Waiting for decoding the masking key (4 bytes) only if the mask bit is set.
    FrameHeaderMaskingKey,
    // Waiting for decoding the payload (both extension data and application data).
    FramePayload,
    // Frame has finished decoding.
    FrameFinished
  };
  uint64_t max_payload_buffer_length_;
  // Current frame that is being decoded.
  Frame frame_;
  State state_ = State::FrameHeaderFlagsAndOpcode;
  uint64_t length_ = 0;
  uint8_t num_remaining_extended_length_bytes_ = 0;
  uint8_t num_remaining_masking_key_bytes_ = 0;
};

} // namespace WebSocket
} // namespace Envoy
