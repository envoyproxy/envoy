#pragma once

#include "envoy/buffer/buffer.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStats {

// The struct to store gRPC message counter state.
struct GrpcMessageCounter {
  // gRPC uses 5 byte header to encode subsequent message length
  enum GrpcReadState {
    ExpectByte0 = 0,
    ExpectByte1,
    ExpectByte2,
    ExpectByte3,
    ExpectByte4,
    ExpectMessage
  };

  // current read state
  GrpcReadState state{ExpectByte0};

  // current message size
  uint64_t current_size{0};

  // message count
  uint64_t count{0};
};

// Detect gRPC message boundaries and increment the counters on a message
// start: each message is prefixed by 5 bytes length-prefix
// https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md. Returns the delta
// increment.
uint64_t IncrementMessageCounter(Buffer::Instance& data, GrpcMessageCounter* counter);

} // namespace GrpcStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
