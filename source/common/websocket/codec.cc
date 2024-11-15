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

absl::optional<std::vector<uint8_t>> Encoder::encodeFrameHeader(const Frame& frame) {
  if (std::find(kFrameOpcodes.begin(), kFrameOpcodes.end(), frame.opcode_) == kFrameOpcodes.end()) {
    ENVOY_LOG(debug, "Failed to encode websocket frame with invalid opcode: {}", frame.opcode_);
    return absl::nullopt;
  }
  std::vector<uint8_t> output;
  // Set flags and opcode
  pushScalarToByteVector(
      static_cast<uint8_t>(frame.final_fragment_ ? (0x80 | frame.opcode_) : frame.opcode_), output);

  // Set payload length
  if (frame.payload_length_ <= 125) {
    // Set mask bit and 7-bit length
    pushScalarToByteVector(frame.masking_key_.has_value()
                               ? static_cast<uint8_t>(frame.payload_length_ | 0x80)
                               : static_cast<uint8_t>(frame.payload_length_),
                           output);
  } else if (frame.payload_length_ <= 65535) {
    // Set mask bit and 16-bit length indicator
    pushScalarToByteVector(static_cast<uint8_t>(frame.masking_key_.has_value() ? 0xfe : 0x7e),
                           output);
    // Set 16-bit length
    pushScalarToByteVector(htobe16(frame.payload_length_), output);
  } else {
    // Set mask bit and 64-bit length indicator
    pushScalarToByteVector(static_cast<uint8_t>(frame.masking_key_.has_value() ? 0xff : 0x7f),
                           output);
    // Set 64-bit length
    pushScalarToByteVector(htobe64(frame.payload_length_), output);
  }
  // Set masking key
  if (frame.masking_key_.has_value()) {
    pushScalarToByteVector(htobe32(frame.masking_key_.value()), output);
  }
  return output;
}

void Decoder::frameDataStart() {
  frame_.payload_length_ = length_;
  if (length_ == 0) {
    state_ = State::FrameFinished;
  } else {
    if (max_payload_buffer_length_ > 0) {
      frame_.payload_ = std::make_unique<Buffer::OwnedImpl>();
    }
    state_ = State::FramePayload;
  }
}

void Decoder::frameData(const uint8_t* mem, uint64_t length) {
  if (max_payload_buffer_length_ > 0) {
    uint64_t allowed_length = max_payload_buffer_length_ - frame_.payload_->length();
    frame_.payload_->add(mem, length <= allowed_length ? length : allowed_length);
  }
}

void Decoder::frameDataEnd(std::vector<Frame>& output) {
  output.push_back(std::move(frame_));
  resetDecoder();
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
    ENVOY_LOG(debug, "Failed to decode websocket frame with invalid opcode: {}", opcode);
    return 0;
  }
  frame_.opcode_ = opcode;
  frame_.final_fragment_ = data.front() & 0x80;
  state_ = State::FrameHeaderMaskFlagAndLength;
  return 1;
}

uint8_t Decoder::doDecodeMaskFlagAndLength(absl::Span<const uint8_t>& data) {
  num_remaining_masking_key_bytes_ = data.front() & 0x80 ? kMaskingKeyLength : 0;
  uint8_t length_indicator = data.front() & 0x7f;
  if (length_indicator == 0x7e) {
    num_remaining_extended_length_bytes_ = kPayloadLength16Bit;
    state_ = State::FrameHeaderExtendedLength16Bit;
  } else if (length_indicator == 0x7f) {
    num_remaining_extended_length_bytes_ = kPayloadLength64Bit;
    state_ = State::FrameHeaderExtendedLength64Bit;
  } else if (num_remaining_masking_key_bytes_ > 0) {
    length_ = length_indicator;
    state_ = State::FrameHeaderMaskingKey;
  } else {
    length_ = length_indicator;
    frameDataStart();
  }
  return 1;
}

uint8_t Decoder::doDecodeExtendedLength(absl::Span<const uint8_t>& data) {
  uint64_t bytes_to_decode = data.length() <= num_remaining_extended_length_bytes_
                                 ? data.length()
                                 : num_remaining_extended_length_bytes_;
  uint8_t size_of_extended_length =
      state_ == State::FrameHeaderExtendedLength16Bit ? kPayloadLength16Bit : kPayloadLength64Bit;
  uint8_t shift_of_bytes = size_of_extended_length - num_remaining_extended_length_bytes_;
  uint8_t* destination = reinterpret_cast<uint8_t*>(&length_) + shift_of_bytes;

  ASSERT(shift_of_bytes >= 0);
  ASSERT(shift_of_bytes < size_of_extended_length);
  memcpy(destination, data.data(), bytes_to_decode); // NOLINT(safe-memcpy)
  num_remaining_extended_length_bytes_ -= bytes_to_decode;

  if (num_remaining_extended_length_bytes_ == 0) {
#if ABSL_IS_BIG_ENDIAN
    length_ = state_ == State::FrameHeaderExtendedLength16Bit ? htole16(le64toh(length_)) : length_;
#else
    length_ = state_ == State::FrameHeaderExtendedLength16Bit ? htobe16(length_) : htobe64(length_);
#endif
    if (num_remaining_masking_key_bytes_ > 0) {
      state_ = State::FrameHeaderMaskingKey;
    } else {
      frameDataStart();
    }
  }
  return bytes_to_decode;
}

uint8_t Decoder::doDecodeMaskingKey(absl::Span<const uint8_t>& data) {
  if (!frame_.masking_key_.has_value()) {
    frame_.masking_key_ = 0;
  }
  uint64_t bytes_to_decode = data.length() <= num_remaining_masking_key_bytes_
                                 ? data.length()
                                 : num_remaining_masking_key_bytes_;
  uint8_t shift_of_bytes = kMaskingKeyLength - num_remaining_masking_key_bytes_;
  uint8_t* destination =
      reinterpret_cast<uint8_t*>(&(frame_.masking_key_.value())) + shift_of_bytes;
  ASSERT(shift_of_bytes >= 0);
  ASSERT(shift_of_bytes < kMaskingKeyLength);
  memcpy(destination, data.data(), bytes_to_decode); // NOLINT(safe-memcpy)
  num_remaining_masking_key_bytes_ -= bytes_to_decode;

  if (num_remaining_masking_key_bytes_ == 0) {
    frame_.masking_key_ = htobe32(frame_.masking_key_.value());
    frameDataStart();
  }
  return bytes_to_decode;
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

absl::optional<std::vector<Frame>> Decoder::decode(const Buffer::Instance& input) {
  absl::optional<std::vector<Frame>> output = std::vector<Frame>();
  for (const Buffer::RawSlice& slice : input.getRawSlices()) {
    absl::Span<const uint8_t> data(reinterpret_cast<uint8_t*>(slice.mem_), slice.len_);
    while (!data.empty() || state_ == State::FrameFinished) {
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
      case State::FrameHeaderExtendedLength16Bit:
      case State::FrameHeaderExtendedLength64Bit:
        bytes_decoded = doDecodeExtendedLength(data);
        break;
      case State::FrameHeaderMaskingKey:
        bytes_decoded = doDecodeMaskingKey(data);
        break;
      case State::FramePayload:
        bytes_decoded = doDecodePayload(data);
        break;
      case State::FrameFinished:
        frameDataEnd(output.value());
        break;
      }
      data.remove_prefix(bytes_decoded);
    }
  }
  return !output->empty() ? std::move(output) : absl::nullopt;
}

} // namespace WebSocket
} // namespace Envoy
