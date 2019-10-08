#pragma once

#include "envoy/buffer/buffer.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStreaming {

// The struct to store gRPC message counter state.
struct GrpcMessageCounter {
  GrpcMessageCounter() : state(ExpectByte0), current_size(0), count(0){};

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
  GrpcReadState state;

  // current message size
  uint64_t current_size;

  // message count
  uint64_t count;
};

// Detect gRPC message boundaries and increment the counters on a message
// start: each message is prefixed by 5 bytes length-prefix
// https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md. Returns true
// if the message count is updated.
bool IncrementMessageCounter(Buffer::Instance& data, GrpcMessageCounter* counter);

} // namespace GrpcStreaming
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
