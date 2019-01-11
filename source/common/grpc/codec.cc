#include "common/grpc/codec.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "common/buffer/buffer_impl.h"
#include "common/common/stack_array.h"

namespace Envoy {
namespace Grpc {

Encoder::Encoder() {}

void Encoder::newFrame(uint8_t flags, uint64_t length, std::array<uint8_t, 5>& output) {
  output[0] = flags;
  output[1] = static_cast<uint8_t>(length >> 24);
  output[2] = static_cast<uint8_t>(length >> 16);
  output[3] = static_cast<uint8_t>(length >> 8);
  output[4] = static_cast<uint8_t>(length);
}

Decoder::Decoder() : state_(State::FH_FLAG) {}

bool Decoder::decode(Buffer::Instance& input, std::vector<Frame>& output) {
  uint64_t count = input.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, count);
  input.getRawSlices(slices.begin(), count);
  for (const Buffer::RawSlice& slice : slices) {
    uint8_t* mem = reinterpret_cast<uint8_t*>(slice.mem_);
    for (uint64_t j = 0; j < slice.len_;) {
      uint8_t c = *mem;
      switch (state_) {
      case State::FH_FLAG:
        if (c & ~GRPC_FH_COMPRESSED) {
          // Unsupported flags.
          return false;
        }
        frame_.flags_ = c;
        state_ = State::FH_LEN_0;
        mem++;
        j++;
        break;
      case State::FH_LEN_0:
        frame_.length_ = static_cast<uint32_t>(c) << 24;
        state_ = State::FH_LEN_1;
        mem++;
        j++;
        break;
      case State::FH_LEN_1:
        frame_.length_ |= static_cast<uint32_t>(c) << 16;
        state_ = State::FH_LEN_2;
        mem++;
        j++;
        break;
      case State::FH_LEN_2:
        frame_.length_ |= static_cast<uint32_t>(c) << 8;
        state_ = State::FH_LEN_3;
        mem++;
        j++;
        break;
      case State::FH_LEN_3:
        frame_.length_ |= static_cast<uint32_t>(c);
        if (frame_.length_ == 0) {
          output.push_back(std::move(frame_));
          state_ = State::FH_FLAG;
        } else {
          frame_.data_ = std::make_unique<Buffer::OwnedImpl>();
          state_ = State::DATA;
        }
        mem++;
        j++;
        break;
      case State::DATA:
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
          state_ = State::FH_FLAG;
        }
        break;
      }
    }
  }
  input.drain(input.length());
  return true;
}

} // namespace Grpc
} // namespace Envoy
