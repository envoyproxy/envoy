#pragma once

#include "source/common/grpc/typed_async_client.h"

namespace Envoy {
namespace Config {

// Oversees communication for gRPC xDS implementations (parent to both SotW xDS and delta
// xDS variants). Reestablishes the gRPC channel when necessary, and provides rate limiting of
// requests.
template <class RequestProto, class ResponseProto>
class GrpcStreamInterface : public Grpc::AsyncStreamCallbacks<ResponseProto> {
public:
  ~GrpcStreamInterface() override = default;

  // Attempt to establish a new gRPC stream to the xDS server.
  virtual void establishNewStream() PURE;

  // Returns true if the gRPC stream is available and messages can be sent over it.
  virtual bool grpcStreamAvailable() const PURE;

  // Sends a request to the xDS server over the stream.
  virtual void sendMessage(const RequestProto& request) PURE;

  // Updates the control_plane_stats `pending_requests` value. Note that the
  // update will not be taken into effect if the size is 0, and the
  // `pending_request` value was not set previously to non-zero value.
  // This is done to avoid updating the queue's length until the first
  // meaningful value is given.
  virtual void maybeUpdateQueueSizeStat(uint64_t size) PURE;

  // Returns true if a message can be sent from the rate-limiting perspective.
  // The rate-limiting counters may be updated by this method.
  virtual bool checkRateLimitAllowsDrain() PURE;

  // Intentionally close the gRPC stream and reset to the pre-establishNewStream() state.
  // Prevents the retry timer from reconnecting.
  virtual void closeStream() PURE;
};

template <class RequestProto, class ResponseProto>
using GrpcStreamInterfacePtr = std::unique_ptr<GrpcStreamInterface<RequestProto, ResponseProto>>;
} // namespace Config
} // namespace Envoy
