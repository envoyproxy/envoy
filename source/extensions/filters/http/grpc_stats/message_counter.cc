#include "extensions/filters/http/grpc_stats/message_counter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStats {

uint64_t IncrementMessageCounter(Buffer::Instance& data, GrpcMessageCounter* counter) {
  uint64_t pos = 0;
  uint8_t byte = 0;
  uint64_t delta = 0;
  while (pos < data.length()) {
    switch (counter->state) {
    case GrpcMessageCounter::ExpectByte0:
      // skip compress flag, increment message count
      counter->count += 1;
      delta += 1;
      counter->current_size = 0;
      pos += 1;
      counter->state = GrpcMessageCounter::ExpectByte1;
      break;
    case GrpcMessageCounter::ExpectByte1:
    case GrpcMessageCounter::ExpectByte2:
    case GrpcMessageCounter::ExpectByte3:
    case GrpcMessageCounter::ExpectByte4:
      data.copyOut(pos, 1, &byte);
      counter->current_size <<= 8;
      counter->current_size |= byte;
      pos += 1;
      counter->state = static_cast<GrpcMessageCounter::GrpcReadState>(counter->state + 1);
      break;
    case GrpcMessageCounter::ExpectMessage:
      uint64_t available = data.length() - pos;
      if (counter->current_size <= available) {
        pos += counter->current_size;
        counter->state = GrpcMessageCounter::ExpectByte0;
      } else {
        pos = data.length();
        counter->current_size -= available;
      }
      break;
    }
  }
  return delta;
}

} // namespace GrpcStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
