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

void Decoder::frameMaskFlag(uint8_t mask_and_length) {
  // Set the mask length to be read
  num_remaining_masking_key_bytes_ = mask_and_length & 0x80 ? kMaskingKeyLength : 0;
  // Set length (0 to 125) or length flag (126 or 127)
  length_ = mask_and_length & 0x7F;
}

void Decoder::frameDataStart() {
  frame_.payload_length_ = length_;
  if (length_ == 0) {
    state_ = State::FrameFinished;
  } else {
    frame_.payload_ = std::make_unique<Buffer::OwnedImpl>();
    state_ = State::FramePayload;
  }
}

void Decoder::frameData(const uint8_t* mem, uint64_t length) { frame_.payload_->add(mem, length); }

void Decoder::frameDataEnd(uint64_t& bytes_consumed_by_frame_, Buffer::Instance& input,
                           absl::optional<std::vector<Frame>>& output) {
  if (!output.has_value()) {
    output = std::vector<Frame>();
  }
  output.value().push_back(std::move(frame_));

  resetDecoder();

  input.drain(bytes_consumed_by_frame_);
  bytes_consumed_by_frame_ = 0;
}

void Decoder::resetDecoder() {
  frame_ = {false, 0, absl::nullopt, 0, nullptr};
  state_ = State::FrameHeaderFlagsAndOpcode;
  length_ = 0;
  num_remaining_extended_length_bytes_ = 0;
  num_remaining_masking_key_bytes_ = 0;
}

uint8_t Decoder::doDecodeFlagsAndOpcode(absl::Span<const uint8_t>& data) {
  // Validate opcode (last 4 bits)
  uint8_t opcode = data.front() & 0x0f;
  if (std::find(kFrameOpcodes.begin(), kFrameOpcodes.end(), opcode) == kFrameOpcodes.end()) {
    return false;
  }
  frame_.opcode_ = opcode;
  frame_.final_fragment_ = data.front() & 0x80;
  state_ = State::FrameHeaderMaskFlagAndLength;
  return 1;
}

uint8_t Decoder::doDecodeMaskFlagAndLength(absl::Span<const uint8_t>& data) {
  frameMaskFlag(data.front());
  if (length_ == 0x7e) {
    num_remaining_extended_length_bytes_ = kPayloadLength16Bit;
    length_ = 0;
    state_ = State::FrameHeaderExtendedLength;
  } else if (length_ == 0x7f) {
    num_remaining_extended_length_bytes_ = kPayloadLength64Bit;
    length_ = 0;
    state_ = State::FrameHeaderExtendedLength;
  } else if (num_remaining_masking_key_bytes_ > 0) {
    state_ = State::FrameHeaderMaskingKey;
  } else {
    frameDataStart();
  }
  return 1;
}

uint8_t Decoder::doDecodeExtendedLength(absl::Span<const uint8_t>& data) {
  if (num_remaining_extended_length_bytes_ > 0) {
    length_ |= static_cast<uint64_t>(data.front())
               << 8 * (num_remaining_extended_length_bytes_ - 1);
  }
  num_remaining_extended_length_bytes_--;
  if (num_remaining_extended_length_bytes_ == 0) {
    if (num_remaining_masking_key_bytes_ > 0) {
      state_ = State::FrameHeaderMaskingKey;
    } else {
      frameDataStart();
    }
  }
  return 1;
}

uint8_t Decoder::doDecodeMaskingKey(absl::Span<const uint8_t>& data) {
  if (!frame_.masking_key_.has_value()) {
    frame_.masking_key_ = 0;
  }
  if (num_remaining_masking_key_bytes_ > 0) {
    frame_.masking_key_.value() |= static_cast<uint32_t>(data.front())
                                   << 8 * (num_remaining_masking_key_bytes_ - 1);
  }
  num_remaining_masking_key_bytes_--;
  if (num_remaining_masking_key_bytes_ == 0) {
    frameDataStart();
  }
  return 1;
}

uint64_t Decoder::doDecodePayload(absl::Span<const uint8_t>& data) {
  uint64_t remain_in_buffer = data.length();
  uint64_t bytes_decoded = 0;
  if (remain_in_buffer <= length_) {
    frameData(data.data(), remain_in_buffer);
    bytes_decoded += remain_in_buffer;
    length_ -= remain_in_buffer;
  } else {
    frameData(data.data(), length_);
    bytes_decoded += length_;
    length_ = 0;
  }
  if (length_ == 0) {
    state_ = State::FrameFinished;
  }
  return bytes_decoded;
}

absl::optional<std::vector<Frame>> Decoder::decode(Buffer::Instance& input) {
  absl::optional<std::vector<Frame>> output = absl::nullopt;
  uint64_t bytes_consumed_by_frame_ = 0;
  resetDecoder();
  for (const Buffer::RawSlice& slice : input.getRawSlices()) {
    absl::Span<const uint8_t> data(reinterpret_cast<uint8_t*>(slice.mem_), slice.len_);
    while (!data.empty()) {
      uint64_t bytes_decoded = 0;
      switch (state_) {
      case State::FrameHeaderFlagsAndOpcode:
        bytes_decoded = doDecodeFlagsAndOpcode(data);
        if (bytes_decoded == 0) {
          return absl::nullopt;
        }
        break;
      case State::FrameHeaderMaskFlagAndLength:
        bytes_decoded = doDecodeMaskFlagAndLength(data);
        break;
      case State::FrameHeaderExtendedLength:
        bytes_decoded = doDecodeExtendedLength(data);
        break;
      case State::FrameHeaderMaskingKey:
        bytes_decoded = doDecodeMaskingKey(data);
        break;
      case State::FramePayload:
        bytes_decoded = doDecodePayload(data);
        break;
      case State::FrameFinished:
        frameDataEnd(bytes_consumed_by_frame_, input, output);
        break;
      }
      data.remove_prefix(bytes_decoded);
      bytes_consumed_by_frame_ += bytes_decoded;
    }
    // Handles when slice ended with a complete frame scenario
    if (state_ == State::FrameFinished) {
      frameDataEnd(bytes_consumed_by_frame_, input, output);
    }
  }
  return output;
}

} // namespace WebSocket
} // namespace Envoy
