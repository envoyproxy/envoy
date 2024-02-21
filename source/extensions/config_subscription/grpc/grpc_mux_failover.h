#pragma once

#include "source/extensions/config_subscription/grpc/grpc_stream.h"

namespace Envoy {
namespace Config {

/**
 * This class arbitrates between two config providers of the same GrpcMux -
 * the primary and the failover. Envoy always prefers fetching config from the
 * primary source, but if not available, will fetch the config from the failover
 * source until the primary is again available.
 *
 * This class owns the state for the GrpcMux primary and failover streams, and
 * proxies the gRPC stream functionality to either the primary or the failover config sources.
 * The failover source is optional and will only be used if passed upon initialization.
 *
 * The primary config source is always preferred over the failover source. If the primary
 * is available, the stream to the failover source will terminated.
 *
 * Failover is supported in both
 * SotW (RequestType = envoy::service::discovery::v3::DiscoveryRequest,
 *   ResponseType = envoy::service::discovery::v3::DiscoveryResponse), and
 * Delta-xDS (RequestType = envoy::service::discovery::v3::DeltaDiscoveryRequest,
 *   ResponseType = envoy::service::discovery::v3::DeltaDiscoveryResponse).
 * Both the primary and failover streams are either SotW or Delta-xDS.
 *
 * The use of this class will be as follows: the GrpcMux object will own an instance of
 * the GrpcMuxFailover. The GrpcMuxFailover will own 2 GrpcStreams, primary and secondary.
 * Each of the primary and secondary streams will invoke GrpcStreamCallbacks
 * on their corresponding objects (also owned by the GrpcMuxFailoverProxy). These invocations
 * will be followed by the GrpcMuxFailoverProxy calling the GrpcStreamCallbacks on the GrpcMux
 * object that initialized it.
 *
 * Note: this class is WIP.
 */
template <class RequestType, class ResponseType> class GrpcMuxFailover {
public:
  // A GrpcStream creator function that receives the stream callbacks and returns a
  // GrpcStream object. This is introduced to facilitate dependency injection for
  // testing and will be used to create the primary and failover streams.
  using GrpcStreamCreator = std::function<GrpcStreamInterfacePtr<RequestType, ResponseType>(
      GrpcStreamCallbacks<ResponseType>* stream_callbacks)>;

  GrpcMuxFailover(GrpcStreamCreator primary_stream_creator,
                  OptRef<GrpcStreamCreator> failover_stream_creator,
                  GrpcStreamCallbacks<ResponseType>& grpc_mux_callbacks)
      : grpc_mux_callbacks_(grpc_mux_callbacks), primary_callbacks_(*this),
        primary_grpc_stream_(std::move(primary_stream_creator(&primary_callbacks_))) {
    ASSERT(primary_grpc_stream_ != nullptr);
    // At the moment failover isn't implemented.
    ASSERT(!failover_stream_creator.has_value());
  }

  virtual ~GrpcMuxFailover() = default;

  void establishNewStream() {
    // TODO(adisuissa): At the moment this is a pass-through method. Once the
    // implementation matures, this call will be updated.
    primary_grpc_stream_->establishNewStream();
  }

  bool grpcStreamAvailable() const {
    // TODO(adisuissa): At the moment this is a pass-through method. Once the
    // implementation matures, this call will be updated.
    return primary_grpc_stream_->grpcStreamAvailable();
  }

  void sendMessage(const RequestType& request) {
    // TODO(adisuissa): At the moment this is a pass-through method. Once the
    // implementation matures, this call will be updated.
    primary_grpc_stream_->sendMessage(request);
  }

  void maybeUpdateQueueSizeStat(uint64_t size) {
    // TODO(adisuissa): At the moment this is a pass-through method. Once the
    // implementation matures, this call will be updated.
    primary_grpc_stream_->maybeUpdateQueueSizeStat(size);
  }

  bool checkRateLimitAllowsDrain() {
    // TODO(adisuissa): At the moment this is a pass-through method. Once the
    // implementation matures, this call will be updated.
    return primary_grpc_stream_->checkRateLimitAllowsDrain();
  }

  absl::optional<Grpc::Status::GrpcStatus> getCloseStatusForTest() {
    // TODO(adisuissa): At the moment this is a pass-through method. Once the
    // implementation matures, this call will be updated.
    return primary_grpc_stream_->getCloseStatusForTest();
  }

  GrpcStreamInterface<RequestType, ResponseType>& currentStreamForTest() {
    // TODO(adisuissa): At the moment this is a pass-through method. Once the
    // implementation matures, this call will be updated.
    return *primary_grpc_stream_.get();
  };

private:
  // A helper class that proxies the callbacks of GrpcStreamCallbacks for the primary service.
  class PrimaryGrpcStreamCallbacks : public GrpcStreamCallbacks<ResponseType> {
  public:
    PrimaryGrpcStreamCallbacks(GrpcMuxFailover& parent) : parent_(parent) {}

    void onStreamEstablished() override {
      // TODO(adisuissa): At the moment this is a pass-through method. Once the
      // implementation matures, this call will be updated.
      parent_.grpc_mux_callbacks_.onStreamEstablished();
    }

    void onEstablishmentFailure() override {
      // TODO(adisuissa): At the moment this is a pass-through method. Once the
      // implementation matures, this call will be updated.
      parent_.grpc_mux_callbacks_.onEstablishmentFailure();
    }

    void onDiscoveryResponse(ResponseProtoPtr<ResponseType>&& message,
                             ControlPlaneStats& control_plane_stats) override {
      // TODO(adisuissa): At the moment this is a pass-through method. Once the
      // implementation matures, this call will be updated.
      parent_.grpc_mux_callbacks_.onDiscoveryResponse(std::move(message), control_plane_stats);
    }

    void onWriteable() override {
      // TODO(adisuissa): At the moment this is a pass-through method. Once the
      // implementation matures, this call will be updated.
      parent_.grpc_mux_callbacks_.onWriteable();
    }

  private:
    GrpcMuxFailover& parent_;
  };

  // Flags to keep track of the state of connections to primary/failover.
  // All initialized to false, as there is no connection process during
  // initialization.
  bool connecting_to_primary_ : 1;
  bool connected_to_primary_ : 1;
  bool connecting_to_failover_ : 1;
  bool connected_to_failover_ : 1;

  // The stream callbacks that will be invoked on the GrpcMux object, to notify
  // about the state of the underlying primary/failover stream.
  GrpcStreamCallbacks<ResponseType>& grpc_mux_callbacks_;
  // The callbacks that will be invoked by the primary stream.
  PrimaryGrpcStreamCallbacks primary_callbacks_;
  // The stream to the primary source.
  GrpcStreamInterfacePtr<RequestType, ResponseType> primary_grpc_stream_;
};

} // namespace Config
} // namespace Envoy
