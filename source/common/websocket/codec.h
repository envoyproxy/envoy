#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "envoy/buffer/buffer.h"

namespace Envoy {
namespace WebSocket {

// opcodes
constexpr uint8_t FRAME_OPCODE_CONT = 0x0;
constexpr uint8_t FRAME_OPCODE_TEXT = 0x1;
constexpr uint8_t FRAME_OPCODE_BIN = 0x2;
constexpr uint8_t FRAME_OPCODE_CLOSE = 0x8;
constexpr uint8_t FRAME_OPCODE_PING = 0x9;
constexpr uint8_t FRAME_OPCODE_PONG = 0xA;

// wire format (https://datatracker.ietf.org/doc/html/rfc6455#section-5.2)
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
//  |                               |Masking-key, if MASK set to 1  |
//  +-------------------------------+-------------------------------+
//  | Masking-key (continued)       |          Payload Data         |
//  +-------------------------------- - - - - - - - - - - - - - - - +
//  :                     Payload Data continued ...                :
//  + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
//  |                     Payload Data continued ...                |
//  +---------------------------------------------------------------+

struct Frame {
  // |F|R|R|R| opcode(4) |
  uint8_t flags_and_opcode_;
  // |M|     length(7)   | max is 125, 126/127 indicates to use 16/64 bits as the length
  bool is_masked_;
  // 7 bits, 7+16 bits, or 7+64 bits (only 63 bits are used in the last case)
  uint64_t payload_length_;
  // This field is present if the mask bit is set to 1 and
  // is absent if the mask bit is set to 0
  uint32_t masking_key_;
  // websocket payload data (extension data and application data)
  Buffer::InstancePtr payload_;

  // Data frames (e.g., non-control frames) are identified by opcodes
  // where the most significant bit of the opcode is 0
  bool isDataFrame;
};

enum class State {
  // Decoding the first byte. Waiting for decoding the final frame flag (1 bit)
  // and reserved flags (3 bits) and opcode (4 bits) of the WebSocket data frame.
  FhFlagsAndOpcode,
  // Decoding the second byte. Waiting for decoding the mask flag (1 bit) and
  // length/length flag (7 bit) of the WebSocket data frame.
  FhMaskFlagAndLength,
  // Waiting for decoding the extended length of the frame if length read previously
  // is either 126 or 127. Respectively 2 bytes or 8 bytes will be decoded from the
  // WebSocket data frame.
  FhExtendedLength,
  // Waiting for decoding the masking key (4 bytes) only if the mask bit is set.
  FhMaskingKey,
  // Waiting for decoding the payload (both extension data and application data).
  Payload,
};

class FrameInspector {
public:
  // Inspects the given buffer with WebSocket data frame and updates the frame count.
  // Invokes visitor callbacks for each frame in the following sequence:
  //   "frameStart frameDataStart frameData* frameDataEnd"
  // If frameStart returns false, then the inspector aborts.
  // Returns the increase in the frame count.
  uint64_t inspect(const Buffer::Instance& input);

  // Returns the current frame count, corresponding to the request/response
  // message count. Counter is incremented on a frame start.
  uint64_t frameCount() const { return count_; }

  // Returns the current state in the frame parsing.
  State state() const { return state_; }

  uint8_t maskingKeyLength() const { return masking_key_length_; }

  virtual ~FrameInspector() = default;

protected:
  virtual bool frameStart(uint8_t) { return true; }
  virtual void frameMaskFlag(uint8_t) {}
  virtual void frameMaskingKey() {}
  virtual void frameDataStart() {}
  virtual void frameData(uint8_t*, uint64_t) {}
  virtual void frameDataEnd() {}

  State state_{State::FhFlagsAndOpcode};
  uint64_t length_{0};
  uint8_t length_of_extended_length_{0};
  uint32_t masking_key_{0};
  uint8_t masking_key_length_{0};
  uint64_t count_{0};
};

class Decoder : public FrameInspector {
public:
  // Decodes the given buffer with WebSocket frame. Drains the input buffer when
  // decoding succeeded (returns true). If the input is not sufficient to make a
  // complete WebSocket frame, it will be buffered in the decoder. If a decoding
  // error happened, the input buffer remains unchanged.
  // @param input supplies the binary octets wrapped in a WebSocket frame.
  // @param output supplies the buffer to store the decoded data.
  // @return bool whether the decoding succeeded or not.
  bool decode(Buffer::Instance& input, std::vector<Frame>& output);

  // Determine the length of the current frame being decoded. This is useful when supplying a
  // partial frame to decode() and wanting to know how many more bytes need to be read to complete
  // the frame.
  uint32_t length() const { return frame_.payload_length_; }

  // Indicates whether it has buffered any partial data.
  // bool hasBufferedData() const { return state_ != State::FhFlag; }
  Frame& getFrame() { return frame_; };

protected:
  bool frameStart(uint8_t) override;
  void frameMaskFlag(uint8_t) override;
  void frameMaskingKey() override;
  void frameDataStart() override;
  void frameData(uint8_t*, uint64_t) override;
  void frameDataEnd() override;

private:
  // Current frame that is being decoded
  Frame frame_;
  // Data holder for successfully decoded frames
  std::vector<Frame>* output_{nullptr};
  bool decoding_error_{false};
  std::vector<uint8_t> frame_opcodes_ = {FRAME_OPCODE_CONT,  FRAME_OPCODE_TEXT, FRAME_OPCODE_BIN,
                                         FRAME_OPCODE_CLOSE, FRAME_OPCODE_PING, FRAME_OPCODE_PONG};
};

} // namespace WebSocket
} // namespace Envoy
