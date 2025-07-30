#pragma once

#include "source/extensions/config_subscription/grpc/grpc_stream.h"

namespace Envoy {
namespace Config {

/**
 * Extended callbacks interface for bidirectional streams.
 * Adds support for receiving DiscoveryRequests from management servers.
 */
template <class RequestProto, class ResponseProto>
class BidirectionalGrpcStreamCallbacks : public GrpcStreamCallbacks<ResponseProto> {
public:
  virtual ~BidirectionalGrpcStreamCallbacks() = default;

  /**
   * Called when a request is received from the management server (reverse xDS).
   * @param message the received request message
   */
  virtual void onDiscoveryRequest(std::unique_ptr<RequestProto>&& message) PURE;
};

/**
 * Bidirectional gRPC stream that can handle both requests and responses.
 * This extends the existing GrpcStream to support receiving DiscoveryRequests
 * from management servers for reverse xDS.
 */
template <class RequestProto, class ResponseProto>
class BidirectionalGrpcStream : public GrpcStream<RequestProto, ResponseProto> {
public:
  BidirectionalGrpcStream(BidirectionalGrpcStreamCallbacks<RequestProto, ResponseProto>* callbacks,
                         Grpc::RawAsyncClientPtr async_client,
                         const Protobuf::MethodDescriptor& service_method,
                         Event::Dispatcher& dispatcher, Stats::Scope& scope,
                         BackOffStrategyPtr backoff_strategy,
                         const RateLimitSettings& rate_limit_settings,
                         typename GrpcStream<RequestProto, ResponseProto>::ConnectedStateValue connected_state_val)
      : GrpcStream<RequestProto, ResponseProto>(callbacks, std::move(async_client), service_method,
                                               dispatcher, scope, std::move(backoff_strategy),
                                               rate_limit_settings, connected_state_val),
        bidirectional_callbacks_(callbacks) {}

  // Override to handle both requests and responses
  void onReceiveMessage(std::unique_ptr<ResponseProto>&& message) override {
    // This is a normal response from the management server
    GrpcStream<RequestProto, ResponseProto>::onReceiveMessage(std::move(message));
  }

  /**
   * Handle incoming request from management server (reverse xDS).
   * This would be called by the gRPC framework when a request is received.
   */
  void onReceiveRequest(std::unique_ptr<RequestProto>&& message) {
    if (bidirectional_callbacks_) {
      bidirectional_callbacks_->onDiscoveryRequest(std::move(message));
    }
  }

  /**
   * Send a response back to the management server.
   * This is used for reverse xDS responses.
   */
  void sendResponse(const ResponseProto& response) {
    if (this->stream_ != nullptr && this->grpcStreamAvailable()) {
      this->stream_->sendMessage(response, false);
    }
  }

private:
  BidirectionalGrpcStreamCallbacks<RequestProto, ResponseProto>* bidirectional_callbacks_;
};

} // namespace Config
} // namespace Envoy 