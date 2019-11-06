#include "common/grpc/codec.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "common/buffer/buffer_impl.h"
#include "common/common/stack_array.h"

namespace Envoy {
namespace Grpc {

Encoder::Encoder() = default;

void Encoder::newFrame(uint8_t flags, uint64_t length, std::array<uint8_t, 5>& output) {
  output[0] = flags;
  output[1] = static_cast<uint8_t>(length >> 24);
  output[2] = static_cast<uint8_t>(length >> 16);
  output[3] = static_cast<uint8_t>(length >> 8);
  output[4] = static_cast<uint8_t>(length);
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
  uint64_t count = data.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, count);
  data.getRawSlices(slices.begin(), count);
  uint64_t delta = 0;
  for (const Buffer::RawSlice& slice : slices) {
    uint8_t* mem = reinterpret_cast<uint8_t*>(slice.mem_);
    for (uint64_t j = 0; j < slice.len_;) {
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
        j++;
        break;
      case State::FhLen0:
        length_ = static_cast<uint32_t>(c) << 24;
        state_ = State::FhLen1;
        mem++;
        j++;
        break;
      case State::FhLen1:
        length_ |= static_cast<uint32_t>(c) << 16;
        state_ = State::FhLen2;
        mem++;
        j++;
        break;
      case State::FhLen2:
        length_ |= static_cast<uint32_t>(c) << 8;
        state_ = State::FhLen3;
        mem++;
        j++;
        break;
      case State::FhLen3:
        length_ |= static_cast<uint32_t>(c);
        frameDataStart();
        if (length_ == 0) {
          frameDataEnd();
          state_ = State::FhFlag;
        } else {
          state_ = State::Data;
        }
        mem++;
        j++;
        break;
      case State::Data:
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
