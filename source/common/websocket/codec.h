#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "envoy/buffer/buffer.h"

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
  // Length of the payload as the number of bytes.
  uint64_t payload_length_;
  // The 4 byte fixed size masking key used to mask the payload. Masking/unmasking should be
  // performed as described in https://datatracker.ietf.org/doc/html/rfc6455#section-5.3
  absl::optional<uint32_t> masking_key_;
  // WebSocket payload data (extension data and application data).
  Buffer::InstancePtr payload_;
};

// Encoder encodes in memory WebSocket frames into frames in the wire format
class Encoder {
public:
  Encoder() = default;

  // Creates a new Websocket data frame header with the given frame data.
  // @param frame supplies the frame to be encoded.
  // @return std::vector<uint8_t> buffer with encoded header data.
  std::vector<uint8_t> newFrameHeader(const Frame& frame);
};

// Current state of the frame that is being processed.
enum class State {
  // Decoding the first byte. Waiting for decoding the final frame flag (1 bit)
  // and reserved flags (3 bits) and opcode (4 bits) of the WebSocket data frame.
  KFrameHeaderFlagsAndOpcode,
  // Decoding the second byte. Waiting for decoding the mask flag (1 bit) and
  // length/length flag (7 bit) of the WebSocket data frame.
  KFrameHeaderMaskFlagAndLength,
  // Waiting for decoding the extended length of the frame if length read previously
  // is either 126 or 127. Respectively 2 bytes or 8 bytes will be decoded from the
  // WebSocket data frame.
  KFrameHeaderExtendedLength,
  // Waiting for decoding the masking key (4 bytes) only if the mask bit is set.
  KFrameHeaderMaskingKey,
  // Waiting for decoding the payload (both extension data and application data).
  KFramePayload,
};

// Inspects the number of frames contains in an input buffer without decoding into frames.
class DecoderBase {
public:
  virtual ~DecoderBase() = default;

protected:
  // Decodes the given buffer with WebSocket data frames and updates the frame count.
  // Invokes visitor callbacks for each frame in the following sequence:
  // ----------------------------------------------------------------------------------
  //  frameStart frameMaskFlag frameMaskingKey? frameDataStart frameData* frameDataEnd
  // ----------------------------------------------------------------------------------
  // If frameStart returns false, then the decoder aborts.
  // Returns the increase in the frame count.
  virtual uint64_t inspect(const Buffer::Instance& input) PURE;

  virtual bool frameStart(uint8_t) PURE;
  virtual void frameMaskFlag(uint8_t) PURE;
  virtual void frameMaskingKey() PURE;
  virtual void frameDataStart() PURE;
  virtual void frameData(const uint8_t*, uint64_t) PURE;
  virtual void frameDataEnd() PURE;

  virtual void doDecodeMaskFlagAndLength(const uint8_t, const uint8_t*&, uint64_t&) PURE;
  virtual void doDecodeExtendedLength(const uint8_t c, const uint8_t*&, uint64_t&) PURE;
  virtual void doDecodeMaskingKey(const uint8_t c, const uint8_t*&, uint64_t&) PURE;
  virtual void doDecodePayload(const Buffer::RawSlice&, const uint8_t*&, uint64_t&) PURE;
};

// Decoder decodes bytes in input buffer into in-memory WebSocket frames.
class Decoder : public DecoderBase {
public:
  // Decodes the given buffer with WebSocket frame. Drains the input buffer when
  // decoding succeeded (returns true). If the input is not sufficient to make a
  // complete WebSocket frame, it will be buffered in the decoder. If a decoding
  // error happened, the input buffer remains unchanged.
  // @param input supplies the binary octets wrapped in a WebSocket frame.
  // @param output supplies the buffer to store the decoded data.
  // @return bool whether the decoding succeeded or not.
  bool decode(Buffer::Instance& input, std::vector<Frame>& output);

protected:
  uint64_t inspect(const Buffer::Instance& input) override;

  bool frameStart(uint8_t) override;
  void frameMaskFlag(uint8_t) override;
  void frameMaskingKey() override;
  void frameDataStart() override;
  void frameData(const uint8_t*, uint64_t) override;
  void frameDataEnd() override;

  void doDecodeMaskFlagAndLength(const uint8_t, const uint8_t*& mem, uint64_t&) override;
  void doDecodeExtendedLength(const uint8_t c, const uint8_t*&, uint64_t&) override;
  void doDecodeMaskingKey(const uint8_t c, const uint8_t*&, uint64_t&) override;
  void doDecodePayload(const Buffer::RawSlice&, const uint8_t*&, uint64_t&) override;

private:
  // Current frame that is being decoded.
  Frame frame_;
  // Data holder for successfully decoded frames.
  std::vector<Frame>* output_ = nullptr;
  bool decoding_error_ = false;
  State state_ = State::KFrameHeaderFlagsAndOpcode;
  uint64_t total_frames_count_ = 0;
  uint8_t length_of_extended_length_ = 0;
  uint32_t masking_key_ = 0;
  uint64_t length_ = 0;
  uint8_t masking_key_length_ = 0;
};

} // namespace WebSocket
} // namespace Envoy
