#include "source/common/websocket/codec.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <algorithm>

#include "source/common/buffer/buffer_impl.h"

#include "absl/container/fixed_array.h"

namespace Envoy {
namespace WebSocket {

bool Decoder::decode(Buffer::Instance& input, std::vector<Frame>& output) {
  decoding_error_ = false;
  output_ = &output;
  inspect(input);
  output_ = nullptr;
  if (decoding_error_) {
    return false;
  }
  input.drain(input.length());
  return true;
}

bool Decoder::frameStart(uint8_t flags_and_opcode) {
  // Validate opcode (last 4 bits)
  uint8_t opcode = flags_and_opcode & 0x0f;
  if (std::find(frame_opcodes.begin(), frame_opcodes.end(), opcode) != frame_opcodes.end()) {
    frame_.flags_and_opcode_ = flags_and_opcode;
    return true;
  }
  decoding_error_ = true;
  return false;
}

uint8_t Decoder::frameMaskAndLength(uint8_t mask_and_length) {
  // Set masked flag
  if (mask_and_length & 0x80) {
    frame_.is_masked_ = true;
  }
  // Set length (0 to 125) or length flag (126 or 127)
  uint8_t length = mask_and_length & 0x7F;
  frame_.payload_length_ = length;
  frame_.payload_ = std::make_unique<Buffer::OwnedImpl>();
  return length;
}

void Decoder::frameDataStart() {
  frame_.payload_length_ = length_;
  frame_.payload_ = std::make_unique<Buffer::OwnedImpl>();
}

void Decoder::frameData(uint8_t* mem, uint64_t length) { frame_.payload_->add(mem, length); }

void Decoder::frameDataEnd() {
  output_->push_back(std::move(frame_));
  frame_.flags_and_opcode_ = 0;
  frame_.payload_length_ = 0;
  frame_.payload_ = nullptr;
  frame_.is_masked_ = false;
}

uint64_t FrameInspector::inspect(const Buffer::Instance& data) {
  uint64_t delta = 0;
  for (const Buffer::RawSlice& slice : data.getRawSlices()) {
    uint8_t* mem = reinterpret_cast<uint8_t*>(slice.mem_);
    for (uint64_t j = 0; j < slice.len_;) {
      uint8_t c = *mem;
      switch (state_) {
      case State::FhFlagsAndOpcode:
        if (!frameStart(c)) {
          return delta;
        }
        count_ += 1;
        delta += 1;
        state_ = State::FhMaskFlagAndLength;
        mem++;
        j++;
        break;
      case State::FhMaskFlagAndLength:
        frameMaskAndLength(c);
        state_ = State::FhExtendedLength;
        mem++;
        j++;
        break;
      case State::FhExtendedLength:
      case State::FhMaskingKey:
      case State::Payload:
      default:
        mem++;
        j++;
        break;
      }
    }
  }
  return delta;
}

} // namespace WebSocket
} // namespace Envoy

// there is a reason to go one byte at a time as 
// the consecutive bytes might be in consecutive slices
