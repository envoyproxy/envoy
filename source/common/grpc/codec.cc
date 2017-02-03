#include "third_party/envoy/src/source/common/grpc/codec.h"

#include "common/buffer/buffer_impl.h"

namespace Grpc {

Encoder::Encoder() {}

void Encoder::NewFrame(uint8_t flags, uint64_t length, uint8_t* output) {
  output[0] = flags;
  output[1] = static_cast<uint8_t>(length >> 24);
  output[2] = static_cast<uint8_t>(length >> 16);
  output[3] = static_cast<uint8_t>(length >> 8);
  output[4] = static_cast<uint8_t>(length);
}

Decoder::Decoder() : state_(State::FH_FLAG) {}

bool Decoder::Decode(Buffer::Instance& input, std::vector<Frame>* output) {
  uint64_t count = input.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[count];
  input.getRawSlices(slices, count);
  for (Buffer::RawSlice& slice : slices) {
    uint8_t* mem = reinterpret_cast<uint8_t*>(slice.mem_);
    for (uint64_t j = 0; j < slice.len_;) {
      uint8_t c = *mem;
      switch (state_) {
      case State::FH_FLAG:
        if (c & ~GRPC_FH_COMPRESSED) {
          // Unsupported flags.
          return false;
        }
        frame_.flags = c;
        state_ = State::FH_LEN_0;
        mem++;
        j++;
        break;
      case State::FH_LEN_0:
        frame_.length = static_cast<uint32_t>(c) << 24;
        state_ = State::FH_LEN_1;
        mem++;
        j++;
        break;
      case State::FH_LEN_1:
        frame_.length |= static_cast<uint32_t>(c) << 16;
        state_ = State::FH_LEN_2;
        mem++;
        j++;
        break;
      case State::FH_LEN_2:
        frame_.length |= static_cast<uint32_t>(c) << 8;
        state_ = State::FH_LEN_3;
        mem++;
        j++;
        break;
      case State::FH_LEN_3:
        frame_.length |= static_cast<uint32_t>(c);
        frame_.data = new Buffer::OwnedImpl();
        state_ = State::DATA;
        mem++;
        j++;
        break;
      case State::DATA:
        uint64_t remain_in_buffer = slice.len_ - j;
        uint64_t remain_in_frame = frame_.length - frame_.data->length();
        if (remain_in_buffer <= remain_in_frame) {
          frame_.data->add(mem, remain_in_buffer);
          mem += remain_in_buffer;
          j += remain_in_buffer;
        } else {
          frame_.data->add(mem, remain_in_frame);
          mem += remain_in_frame;
          j += remain_in_frame;
        }
        if (frame_.length == frame_.data->length()) {
          output->push_back(frame_); // make a copy.
          frame_.flags = 0;
          frame_.length = 0;
          frame_.data = nullptr;
          state_ = State::FH_FLAG;
        }
        break;
      }
    }
  }
  return true;
}

} // namespace Grpc
