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

bool Decoder::doDecodeFlagsAndOpcode(uint8_t flags_and_opcode) {
  // Validate opcode (last 4 bits)
  uint8_t opcode = flags_and_opcode & 0x0f;
  if (std::find(kFrameOpcodes.begin(), kFrameOpcodes.end(), opcode) == kFrameOpcodes.end()) {
    return false;
  }
  frame_.opcode_ = opcode;
  frame_.final_fragment_ = flags_and_opcode & 0x80;
  state_ = State::FrameHeaderMaskFlagAndLength;
  return true;
}

void Decoder::frameMaskFlag(uint8_t mask_and_length) {
  // Set the mask length to be read
  masking_key_length_ = mask_and_length & 0x80 ? kMaskingKeyLength : 0;
  // Set length (0 to 125) or length flag (126 or 127)
  length_ = mask_and_length & 0x7F;
}

void Decoder::frameDataStart(Buffer::Instance& input, absl::optional<std::vector<Frame>>& output) {
  frame_.payload_length_ = length_;
  if (length_ == 0) {
    frameDataEnd(input, output);
    state_ = State::FrameHeaderFlagsAndOpcode;
  } else {
    frame_.payload_ = std::make_unique<Buffer::OwnedImpl>();
    state_ = State::FramePayload;
  }
}

void Decoder::frameData(const uint8_t* mem, uint64_t length) { frame_.payload_->add(mem, length); }

void Decoder::frameDataEnd(Buffer::Instance& input, absl::optional<std::vector<Frame>>& output) {
  if (!output.has_value()) {
    output = std::vector<Frame>();
  }
  output.value().push_back(std::move(frame_));
  frame_.final_fragment_ = false;
  frame_.opcode_ = 0;
  frame_.payload_length_ = 0;
  frame_.payload_ = nullptr;
  frame_.masking_key_ = absl::nullopt;
  input.drain(bytes_consumed_by_frame_);
  bytes_consumed_by_frame_ = 0;
}

void Decoder::doDecodeMaskFlagAndLength(const uint8_t flag_and_length, Buffer::Instance& input,
                                        absl::optional<std::vector<Frame>>& output) {
  frameMaskFlag(flag_and_length);
  if (length_ == 0x7e) {
    length_of_extended_length_ = kPayloadLength16Bit;
    length_ = 0;
    state_ = State::FrameHeaderExtendedLength;
  } else if (length_ == 0x7f) {
    length_of_extended_length_ = kPayloadLength64Bit;
    length_ = 0;
    state_ = State::FrameHeaderExtendedLength;
  } else if (masking_key_length_ > 0) {
    state_ = State::FrameHeaderMaskingKey;
  } else {
    frameDataStart(input, output);
  }
}

void Decoder::doDecodeExtendedLength(const uint8_t length, Buffer::Instance& input,
                                     absl::optional<std::vector<Frame>>& output) {
  if (length_of_extended_length_ == 1) {
    length_ |= static_cast<uint64_t>(length);
  } else {
    length_ |= static_cast<uint64_t>(length) << 8 * (length_of_extended_length_ - 1);
  }
  length_of_extended_length_--;
  if (length_of_extended_length_ == 0) {
    if (masking_key_length_ > 0) {
      state_ = State::FrameHeaderMaskingKey;
    } else {
      frameDataStart(input, output);
    }
  }
}

void Decoder::doDecodeMaskingKey(const uint8_t mask, Buffer::Instance& input,
                                 absl::optional<std::vector<Frame>>& output) {
  if (!frame_.masking_key_.has_value()) {
    frame_.masking_key_ = 0;
  }
  if (masking_key_length_ == 1) {
    frame_.masking_key_.value() |= static_cast<uint32_t>(mask);
  } else {
    frame_.masking_key_.value() |= static_cast<uint32_t>(mask) << 8 * (masking_key_length_ - 1);
  }
  masking_key_length_--;
  if (masking_key_length_ == 0) {
    frameDataStart(input, output);
  }
}

void Decoder::doDecodePayload(const uint64_t slice_length, const uint8_t*& mem,
                              uint64_t& slice_index, Buffer::Instance& input,
                              absl::optional<std::vector<Frame>>& output) {
  uint64_t remain_in_buffer = slice_length - slice_index;
  if (remain_in_buffer <= length_) {
    frameData(mem, remain_in_buffer);
    mem += remain_in_buffer;
    slice_index += remain_in_buffer;
    bytes_consumed_by_frame_ += remain_in_buffer;
    length_ -= remain_in_buffer;
  } else {
    frameData(mem, length_);
    mem += length_;
    slice_index += length_;
    bytes_consumed_by_frame_ += length_;
    length_ = 0;
  }
  if (length_ == 0) {
    frameDataEnd(input, output);
    state_ = State::FrameHeaderFlagsAndOpcode;
  }
}

absl::optional<std::vector<Frame>> Decoder::decode(Buffer::Instance& input) {
  absl::optional<std::vector<Frame>> output = absl::nullopt;
  if (!inspect(input, output)) {
    return absl::nullopt;
  }
  return output;
}

bool Decoder::inspect(Buffer::Instance& input, absl::optional<std::vector<Frame>>& output) {
  for (const Buffer::RawSlice& slice : input.getRawSlices()) {
    const uint8_t* mem = reinterpret_cast<uint8_t*>(slice.mem_);
    for (uint64_t slice_index = 0; slice_index < slice.len_;) {
      uint8_t byte_of_frame = *mem;
      switch (state_) {
      case State::FrameHeaderFlagsAndOpcode:
        if (!doDecodeFlagsAndOpcode(byte_of_frame)) {
          return false;
        }
        mem++;
        slice_index++;
        bytes_consumed_by_frame_++;
        break;
      case State::FrameHeaderMaskFlagAndLength:
        doDecodeMaskFlagAndLength(byte_of_frame, input, output);
        mem++;
        slice_index++;
        bytes_consumed_by_frame_++;
        break;
      case State::FrameHeaderExtendedLength:
        doDecodeExtendedLength(byte_of_frame, input, output);
        mem++;
        slice_index++;
        bytes_consumed_by_frame_++;
        break;
      case State::FrameHeaderMaskingKey:
        doDecodeMaskingKey(byte_of_frame, input, output);
        mem++;
        slice_index++;
        bytes_consumed_by_frame_++;
        break;
      case State::FramePayload:
        doDecodePayload(slice.len_, mem, slice_index, input, output);
        break;
      }
    }
  }
  return true;
}

} // namespace WebSocket
} // namespace Envoy
