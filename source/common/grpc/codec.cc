#include "source/common/grpc/codec.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "source/common/buffer/buffer_impl.h"

#include "absl/container/fixed_array.h"

namespace Envoy {
namespace Grpc {

Encoder::Encoder() = default;

void Encoder::newFrame(uint8_t flags, uint64_t length, std::array<uint8_t, 5>& output) {
  output[0] = flags;
  absl::big_endian::Store32(&output[1], length);
}

void Encoder::prependFrameHeader(uint8_t flags, Buffer::Instance& buffer) {
  prependFrameHeader(flags, buffer, buffer.length());
}

void Encoder::prependFrameHeader(uint8_t flags, Buffer::Instance& buffer, uint32_t message_length) {
  // Compute the size of the payload and construct the length prefix.
  std::array<uint8_t, Grpc::GRPC_FRAME_HEADER_SIZE> frame;
  Grpc::Encoder().newFrame(flags, message_length, frame);
  Buffer::OwnedImpl frame_buffer(frame.data(), frame.size());
  buffer.prepend(frame_buffer);
}

absl::Status Decoder::decode(Buffer::Instance& input, std::vector<Frame>& output) {
  // Make sure those flags are set to initial state.
  decoding_error_ = false;
  is_frame_oversized_ = false;
  output_ = &output;
  inspect(input);
  output_ = nullptr;

  if (decoding_error_) {
    return absl::InternalError("Grpc decoding error");
  }

  if (is_frame_oversized_) {
    return absl::ResourceExhaustedError("Grpc frame length exceeds limit");
  }

  input.drain(input.length());
  return absl::OkStatus();
}

bool Decoder::frameStart(uint8_t flags) {
  // Unsupported flags.
  if (flags & ~GRPC_FH_COMPRESSED) {
    decoding_error_ = true;
    return false;
  }
  frame_.flags_ = flags;
  return true;
}

void Decoder::frameDataStart() {
  frame_.length_ = length_;
  frame_.data_ = std::make_unique<Buffer::OwnedImpl>();
}

void Decoder::frameData(uint8_t* mem, uint64_t length) { frame_.data_->add(mem, length); }

void Decoder::frameDataEnd() {
  output_->push_back(std::move(frame_));
  frame_.flags_ = 0;
  frame_.length_ = 0;
  frame_.data_ = nullptr;
}

uint64_t FrameInspector::inspect(const Buffer::Instance& data) {
  uint64_t delta = 0;
  for (const Buffer::RawSlice& slice : data.getRawSlices()) {
    uint8_t* mem = reinterpret_cast<uint8_t*>(slice.mem_);
    uint8_t* end = mem + slice.len_;
    while (mem < end) {
      uint8_t c = *mem;
      switch (state_) {
      case State::FhFlag:
        if (!frameStart(c)) {
          return delta;
        }
        count_ += 1;
        delta += 1;
        state_ = State::FhLen0;
        mem++;
        break;
      case State::FhLen0:
        length_as_bytes_[0] = c;
        state_ = State::FhLen1;
        mem++;
        break;
      case State::FhLen1:
        length_as_bytes_[1] = c;
        state_ = State::FhLen2;
        mem++;
        break;
      case State::FhLen2:
        length_as_bytes_[2] = c;
        state_ = State::FhLen3;
        mem++;
        break;
      case State::FhLen3:
        length_as_bytes_[3] = c;
        length_ = absl::big_endian::Load32(length_as_bytes_);
        // Compares the frame length against maximum length when `max_frame_length_` is configured,
        if (max_frame_length_ != 0 && length_ > max_frame_length_) {
          // Set the flag to indicate the over-limit error and return.
          is_frame_oversized_ = true;
          return delta;
        }
        frameDataStart();
        if (length_ == 0) {
          frameDataEnd();
          state_ = State::FhFlag;
        } else {
          state_ = State::Data;
        }
        mem++;
        break;
      case State::Data:
        uint64_t remain_in_buffer = end - mem;
        if (remain_in_buffer <= length_) {
          frameData(mem, remain_in_buffer);
          mem += remain_in_buffer;
          length_ -= remain_in_buffer;
        } else {
          frameData(mem, length_);
          mem += length_;
          length_ = 0;
        }
        if (length_ == 0) {
          frameDataEnd();
          state_ = State::FhFlag;
        }
        break;
      }
    }
  }
  return delta;
}

} // namespace Grpc
} // namespace Envoy
