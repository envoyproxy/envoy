#include "source/common/websocket/codec.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/scalar_to_byte_vector.h"

namespace Envoy {
namespace WebSocket {

Encoder::Encoder() = default;

void Encoder::newFrameHeader(const Frame& frame, std::vector<uint8_t>& output) {
  // Set flags and opcode
  pushScalarToByteVector(frame.flags_and_opcode_, output);

  // Set payload length
  if (frame.payload_length_ <= 125) {
    // Set mask bit and 7-bit length
    pushScalarToByteVector(frame.masking_key_ ? static_cast<uint8_t>(frame.payload_length_ | 0x80)
                                              : static_cast<uint8_t>(frame.payload_length_),
                           output);
  } else if (frame.payload_length_ <= 65535) {
    // Set mask bit and 16-bit length indicator
    pushScalarToByteVector(static_cast<uint8_t>(frame.masking_key_ ? 0xfe : 0x7e), output);
    // Set 16-bit length
    for (uint8_t i = 1; i <= PAYLOAD_LENGTH_SIXTEEN_BIT; i++) {
      pushScalarToByteVector(
          static_cast<uint8_t>(frame.payload_length_ >> (8 * (PAYLOAD_LENGTH_SIXTEEN_BIT - i))),
          output);
    }
  } else {
    // Set mask bit and 64-bit length indicator
    pushScalarToByteVector(static_cast<uint8_t>(frame.masking_key_ ? 0xff : 0x7f), output);
    // Set 64-bit length
    for (uint8_t i = 1; i <= PAYLOAD_LENGTH_SIXTY_FOUR_BIT; i++) {
      pushScalarToByteVector(
          static_cast<uint8_t>(frame.payload_length_ >> (8 * (PAYLOAD_LENGTH_SIXTY_FOUR_BIT - i))),
          output);
    }
  }
  // Set masking key
  if (frame.masking_key_) {
    for (uint8_t i = 1; i <= MASKING_KEY_LENGTH; i++) {
      pushScalarToByteVector(
          static_cast<uint8_t>(frame.masking_key_.value() >> (8 * (MASKING_KEY_LENGTH - i))),
          output);
    }
  }
}

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
  if (std::find(FRAME_OPCODES.begin(), FRAME_OPCODES.end(), opcode) != FRAME_OPCODES.end()) {
    frame_.flags_and_opcode_ = flags_and_opcode;
    return true;
  }
  decoding_error_ = true;
  return false;
}

void Decoder::frameMaskFlag(uint8_t mask_and_length) {
  // Set masked flag
  if (mask_and_length & 0x80) {
    masking_key_length_ = MASKING_KEY_LENGTH;
  } else {
    masking_key_length_ = 0;
  }
  // Set length (0 to 125) or length flag (126 or 127)
  length_ = mask_and_length & 0x7F;
}

void Decoder::frameMaskingKey() {
  frame_.masking_key_ = masking_key_;
  masking_key_ = 0;
}

void Decoder::frameDataStart() {
  frame_.payload_length_ = length_;
  frame_.payload_ = std::make_unique<Buffer::OwnedImpl>();
}

void Decoder::frameData(const uint8_t* mem, uint64_t length) { frame_.payload_->add(mem, length); }

void Decoder::frameDataEnd() {
  output_->push_back(std::move(frame_));
  frame_.flags_and_opcode_ = 0;
  frame_.payload_length_ = 0;
  frame_.payload_ = nullptr;
  frame_.masking_key_ = absl::nullopt;
}

uint64_t FrameInspector::inspect(const Buffer::Instance& data) {
  uint64_t frames_count_ = 0;
  for (const Buffer::RawSlice& slice : data.getRawSlices()) {
    const uint8_t* mem = reinterpret_cast<uint8_t*>(slice.mem_);
    for (uint64_t j = 0; j < slice.len_;) {
      uint8_t c = *mem;
      switch (state_) {
      case State::FrameHeaderFinalFlagReservedFlagsOpcode:
        if (!frameStart(c)) {
          return frames_count_;
        }
        total_frames_count_ += 1;
        frames_count_ += 1;
        state_ = State::FrameHeaderMaskFlagAndLength;
        mem++;
        j++;
        break;
      case State::FrameHeaderMaskFlagAndLength:
        frameMaskFlag(c);
        if (length_ == 0x7e) {
          length_of_extended_length_ = PAYLOAD_LENGTH_SIXTEEN_BIT;
          length_ = 0;
          state_ = State::FrameHeaderExtendedLength;
        } else if (length_ == 0x7f) {
          length_of_extended_length_ = PAYLOAD_LENGTH_SIXTY_FOUR_BIT;
          length_ = 0;
          state_ = State::FrameHeaderExtendedLength;
        } else if (masking_key_length_ > 0) {
          state_ = State::FrameHeaderMaskingKey;
        } else {
          frameDataStart();
          if (length_ == 0) {
            frameDataEnd();
            state_ = State::FrameHeaderFinalFlagReservedFlagsOpcode;
          } else {
            state_ = State::FramePayload;
          }
        }
        mem++;
        j++;
        break;
      case State::FrameHeaderExtendedLength:
        if (length_of_extended_length_ == 1) {
          length_ |= static_cast<uint64_t>(c);
        } else {
          length_ |= static_cast<uint64_t>(c) << 8 * (length_of_extended_length_ - 1);
        }
        length_of_extended_length_--;
        if (length_of_extended_length_ == 0) {
          if (masking_key_length_ > 0) {
            state_ = State::FrameHeaderMaskingKey;
          } else {
            frameDataStart();
            if (length_ == 0) {
              frameDataEnd();
              state_ = State::FrameHeaderFinalFlagReservedFlagsOpcode;
            } else {
              state_ = State::FramePayload;
            }
          }
        }
        mem++;
        j++;
        break;
      case State::FrameHeaderMaskingKey:
        if (masking_key_length_ == 1) {
          masking_key_ |= static_cast<uint32_t>(c);
        } else {
          masking_key_ |= static_cast<uint32_t>(c) << 8 * (masking_key_length_ - 1);
        }
        masking_key_length_--;
        if (masking_key_length_ == 0) {
          frameMaskingKey();
          frameDataStart();
          if (length_ == 0) {
            frameDataEnd();
            state_ = State::FrameHeaderFinalFlagReservedFlagsOpcode;
          } else {
            state_ = State::FramePayload;
          }
        }
        mem++;
        j++;
        break;
      case State::FramePayload:
        uint64_t remain_in_buffer = slice.len_ - j;
        if (remain_in_buffer <= length_) {
          frameData(mem, remain_in_buffer);
          mem += remain_in_buffer;
          j += remain_in_buffer;
          length_ -= remain_in_buffer;
        } else {
          frameData(mem, length_);
          mem += length_;
          j += length_;
          length_ = 0;
        }
        if (length_ == 0) {
          frameDataEnd();
          state_ = State::FrameHeaderFinalFlagReservedFlagsOpcode;
        }
        break;
      }
    }
  }
  return frames_count_;
}

} // namespace WebSocket
} // namespace Envoy
