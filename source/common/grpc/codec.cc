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

Decoder::Decoder() : state_(State::FhFlag) {}

bool Decoder::decode(Buffer::Instance& input, std::vector<Frame>& output) {
  uint64_t count = input.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, count);
  input.getRawSlices(slices.begin(), count);
  for (const Buffer::RawSlice& slice : slices) {
    uint8_t* mem = reinterpret_cast<uint8_t*>(slice.mem_);
    for (uint64_t j = 0; j < slice.len_;) {
      uint8_t c = *mem;
      switch (state_) {
      case State::FhFlag:
        if (c & ~GRPC_FH_COMPRESSED) {
          // Unsupported flags.
          return false;
        }
        frame_.flags_ = c;
        state_ = State::FhLen0;
        mem++;
        j++;
        break;
      case State::FhLen0:
        frame_.length_ = static_cast<uint32_t>(c) << 24;
        state_ = State::FhLen1;
        mem++;
        j++;
        break;
      case State::FhLen1:
        frame_.length_ |= static_cast<uint32_t>(c) << 16;
        state_ = State::FhLen2;
        mem++;
        j++;
        break;
      case State::FhLen2:
        frame_.length_ |= static_cast<uint32_t>(c) << 8;
        state_ = State::FhLen3;
        mem++;
        j++;
        break;
      case State::FhLen3:
        frame_.length_ |= static_cast<uint32_t>(c);
        if (frame_.length_ == 0) {
          output.push_back(std::move(frame_));
          state_ = State::FhFlag;
        } else {
          frame_.data_ = std::make_unique<Buffer::OwnedImpl>();
          state_ = State::Data;
        }
        mem++;
        j++;
        break;
      case State::Data:
        uint64_t remain_in_buffer = slice.len_ - j;
        uint64_t remain_in_frame = frame_.length_ - frame_.data_->length();
        if (remain_in_buffer <= remain_in_frame) {
          frame_.data_->add(mem, remain_in_buffer);
          mem += remain_in_buffer;
          j += remain_in_buffer;
        } else {
          frame_.data_->add(mem, remain_in_frame);
          mem += remain_in_frame;
          j += remain_in_frame;
        }
        if (frame_.length_ == frame_.data_->length()) {
          output.push_back(std::move(frame_));
          frame_.flags_ = 0;
          frame_.length_ = 0;
          state_ = State::FhFlag;
        }
        break;
      }
    }
  }
  input.drain(input.length());
  return true;
}

uint64_t FrameInspector::decode(const Buffer::Instance& data) {
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
        if (length_ == 0) {
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
          mem += remain_in_buffer;
          j += remain_in_buffer;
          length_ -= remain_in_buffer;
        } else {
          mem += length_;
          j += length_;
          length_ = 0;
        }
        if (length_ == 0) {
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
