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

std::vector<uint8_t> Encoder::encodeFrameHeader(const Frame& frame) {
  std::vector<uint8_t> output;
  // Set flags and opcode
  pushScalarToByteVector(
      static_cast<uint8_t>(frame.final_fragment_ ? (0x80 | frame.opcode_) : frame.opcode_), output);

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
    pushScalarToByteVector(htobe16(frame.payload_length_), output);
  } else {
    // Set mask bit and 64-bit length indicator
    pushScalarToByteVector(static_cast<uint8_t>(frame.masking_key_ ? 0xff : 0x7f), output);
    // Set 64-bit length
    pushScalarToByteVector(htobe64(frame.payload_length_), output);
  }
  // Set masking key
  if (frame.masking_key_) {
    pushScalarToByteVector(htobe32(frame.masking_key_.value()), output);
  }
  return output;
}

bool Decoder::frameStart(uint8_t flags_and_opcode) {
  // Validate opcode (last 4 bits)
  uint8_t opcode = flags_and_opcode & 0x0f;
  if (std::find(kFrameOpcodes.begin(), kFrameOpcodes.end(), opcode) != kFrameOpcodes.end()) {
    frame_.opcode_ = opcode;
    // Set final fragment flag
    frame_.final_fragment_ = flags_and_opcode & 0x80;
    return true;
  }
  decoding_error_ = true;
  return false;
}

void Decoder::frameMaskFlag(uint8_t mask_and_length) {
  // Set masked flag
  if (mask_and_length & 0x80) {
    masking_key_length_ = kMaskingKeyLength;
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
  if (length_ == 0) {
    frameDataEnd();
    state_ = State::KFrameHeaderFlagsAndOpcode;
  } else {
    state_ = State::KFramePayload;
  }
}

void Decoder::frameData(const uint8_t* mem, uint64_t length) { frame_.payload_->add(mem, length); }

void Decoder::frameDataEnd() {
  if (!output_) {
    output_ = std::vector<Frame>();
  }
  output_.value().push_back(std::move(frame_));
  frame_.final_fragment_ = false;
  frame_.opcode_ = 0;
  frame_.payload_length_ = 0;
  frame_.payload_ = nullptr;
  frame_.masking_key_ = absl::nullopt;
}

void Decoder::doDecodeMaskFlagAndLength(const uint8_t c, const uint8_t*& mem,
                                        uint64_t& slice_index) {
  frameMaskFlag(c);
  if (length_ == 0x7e) {
    length_of_extended_length_ = kPayloadLength16Bit;
    length_ = 0;
    state_ = State::KFrameHeaderExtendedLength;
  } else if (length_ == 0x7f) {
    length_of_extended_length_ = kPayloadLength64Bit;
    length_ = 0;
    state_ = State::KFrameHeaderExtendedLength;
  } else if (masking_key_length_ > 0) {
    state_ = State::KFrameHeaderMaskingKey;
  } else {
    frameDataStart();
  }
  slice_index++;
  mem++;
}

void Decoder::doDecodeExtendedLength(const uint8_t c, const uint8_t*& mem, uint64_t& slice_index) {
  if (length_of_extended_length_ == 1) {
    length_ |= static_cast<uint64_t>(c);
  } else {
    length_ |= static_cast<uint64_t>(c) << 8 * (length_of_extended_length_ - 1);
  }
  length_of_extended_length_--;
  if (length_of_extended_length_ == 0) {
    if (masking_key_length_ > 0) {
      state_ = State::KFrameHeaderMaskingKey;
    } else {
      frameDataStart();
    }
  }
  slice_index++;
  mem++;
}

void Decoder::doDecodeMaskingKey(const uint8_t c, const uint8_t*& mem, uint64_t& slice_index) {
  if (masking_key_length_ == 1) {
    masking_key_ |= static_cast<uint32_t>(c);
  } else {
    masking_key_ |= static_cast<uint32_t>(c) << 8 * (masking_key_length_ - 1);
  }
  masking_key_length_--;
  if (masking_key_length_ == 0) {
    frameMaskingKey();
    frameDataStart();
  }
  slice_index++;
  mem++;
}

void Decoder::doDecodePayload(const Buffer::RawSlice& slice, const uint8_t*& mem,
                              uint64_t& slice_index) {
  uint64_t remain_in_buffer = slice.len_ - slice_index;
  if (remain_in_buffer <= length_) {
    frameData(mem, remain_in_buffer);
    mem += remain_in_buffer;
    slice_index += remain_in_buffer;
    length_ -= remain_in_buffer;
  } else {
    frameData(mem, length_);
    mem += length_;
    slice_index += length_;
    length_ = 0;
  }
  if (length_ == 0) {
    frameDataEnd();
    state_ = State::KFrameHeaderFlagsAndOpcode;
  }
}

absl::optional<std::vector<Frame>> Decoder::decode(Buffer::Instance& input) {
  decoding_error_ = false;
  inspect(input);
  if (decoding_error_) {
    return absl::nullopt;
  }
  input.drain(input.length());
  return std::move(output_);
}

uint64_t Decoder::inspect(const Buffer::Instance& data) {
  uint64_t frames_count_ = 0;
  for (const Buffer::RawSlice& slice : data.getRawSlices()) {
    const uint8_t* mem = reinterpret_cast<uint8_t*>(slice.mem_);
    for (uint64_t slice_index = 0; slice_index < slice.len_;) {
      uint8_t c = *mem;
      switch (state_) {
      case State::KFrameHeaderFlagsAndOpcode:
        if (!frameStart(c)) {
          return frames_count_;
        }
        total_frames_count_ += 1;
        frames_count_ += 1;
        state_ = State::KFrameHeaderMaskFlagAndLength;
        mem++;
        slice_index++;
        break;
      case State::KFrameHeaderMaskFlagAndLength:
        doDecodeMaskFlagAndLength(c, mem, slice_index);
        break;
      case State::KFrameHeaderExtendedLength:
        doDecodeExtendedLength(c, mem, slice_index);
        break;
      case State::KFrameHeaderMaskingKey:
        doDecodeMaskingKey(c, mem, slice_index);
        break;
      case State::KFramePayload:
        doDecodePayload(slice, mem, slice_index);
        break;
      }
    }
  }
  return frames_count_;
}

} // namespace WebSocket
} // namespace Envoy
