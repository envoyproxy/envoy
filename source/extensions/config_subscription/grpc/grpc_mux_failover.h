#pragma once

#include "source/common/common/logger.h"
#include "source/extensions/config_subscription/grpc/grpc_stream.h"

namespace Envoy {
namespace Config {

/**
 * This class arbitrates between two config providers of the same GrpcMux -
 * the primary and the failover. Envoy always prefers fetching config from the
 * primary source, but if not available, it will fetch the config from the failover
 * source until the primary is again available.
 *
 * This class owns the state for the GrpcMux primary and failover streams, and
 * proxies the gRPC-stream functionality to either the primary or the failover config sources.
 * The failover source is optional and will only be used if given in the c'tor.
 *
 * Failover is supported in both
 * SotW (RequestType = envoy::service::discovery::v3::DiscoveryRequest,
 *   ResponseType = envoy::service::discovery::v3::DiscoveryResponse), and
 * Delta-xDS (RequestType = envoy::service::discovery::v3::DeltaDiscoveryRequest,
 *   ResponseType = envoy::service::discovery::v3::DeltaDiscoveryResponse).
 * Both the primary and failover streams are either SotW or Delta-xDS.
 *
 * The use of this class will be as follows: the GrpcMux object will own an instance of
 * the GrpcMuxFailover. The GrpcMuxFailover will own 2 GrpcStreams, primary and failover.
 * Each of the primary and secondary streams will invoke GrpcStreamCallbacks
 * on their corresponding objects (also owned by the GrpcMuxFailover). These invocations
 * will be followed by the GrpcMuxFailover calling the GrpcStreamCallbacks on the GrpcMux
 * object that initialized it.
 *
 * Note: this class is WIP and should be considered alpha!
 * At the moment, the supported policy is as follows:
 * The GrpcMuxFailover attempts to establish a connection to the primary source. Once a response is
 * received from the primary source it will be considered available, and the failover will not be
 * used. Any future reconnection attempts will be to the primary source only.
 * However, if no response is received from the primary source, and accessing the primary has
 * failed 2 times in a row, the GrpcMuxFailover will attempt to establish a connection to the
 * failover source. If a response from the failover source is received, only the failover source
 * will be used.
 * If the failover source is unavailable, the GrpcMuxFailover will alternate between attempts
 * to reconnect to the primary source and the failover source.
 * In the future, this behavior may change, and the GrpcMuxFailover will always
 * prefer the primary source, even if prior connection to the failover was successful.
 * TODO(adisuissa): The number of consecutive failures is currently statically
 * defined, and may be converted to a config field in the future.
 */
template <class RequestType, class ResponseType>
class GrpcMuxFailover : public GrpcStreamInterface<RequestType, ResponseType>,
                        public Logger::Loggable<Logger::Id::config> {
public:
  static constexpr uint32_t DefaultFailoverBackoffMilliseconds = 500;

  // A GrpcStream creator function that receives the stream callbacks and returns a
  // GrpcStream object. This is introduced to facilitate dependency injection for
  // testing and will be used to create the primary and failover streams.
  using GrpcStreamCreator = std::function<GrpcStreamInterfacePtr<RequestType, ResponseType>(
      GrpcStreamCallbacks<ResponseType>* stream_callbacks)>;

  GrpcMuxFailover(GrpcStreamCreator primary_stream_creator,
                  absl::optional<GrpcStreamCreator> failover_stream_creator,
                  GrpcStreamCallbacks<ResponseType>& grpc_mux_callbacks,
                  Event::Dispatcher& dispatcher)
      : grpc_mux_callbacks_(grpc_mux_callbacks), primary_callbacks_(*this),
        primary_grpc_stream_(std::move(primary_stream_creator(&primary_callbacks_))),
        connecting_to_primary_(false), connecting_to_failover_(false),
        connected_to_(ConnectedState::None), ever_connected_to_(ConnectedState::None) {
    ASSERT(primary_grpc_stream_ != nullptr);
    if (failover_stream_creator.has_value()) {
      ENVOY_LOG(warn, "Using xDS-Failover. Note that the implementation is currently considered "
                      "experimental and may be modified in future Envoy versions!");
      // Only create the retry timer if failover is supported.
      complete_retry_timer_ = dispatcher.createTimer([this]() -> void { retryConnections(); });
      failover_callbacks_ = std::make_unique<FailoverGrpcStreamCallbacks>(*this);
      GrpcStreamCreator& failover_stream_creator_ref = failover_stream_creator.value();
      failover_grpc_stream_ = std::move(failover_stream_creator_ref(failover_callbacks_.get()));
      ASSERT(failover_grpc_stream_ != nullptr);
    }
  }

  virtual ~GrpcMuxFailover() = default;

  // Attempts to establish a new stream to the either the primary or failover source.
  void establishNewStream() override {
    // Attempt establishing a connection to the primary source.
    // This method may be called multiple times, even if the primary stream is already
    // established or in the process of being established.
    if (complete_retry_timer_) {
      complete_retry_timer_->disableTimer();
    }
    // First check if Envoy ever connected to the primary/failover, and if so
    // persist attempts to that source.
    if (ever_connected_to_ == ConnectedState::Primary) {
      ASSERT(!connecting_to_failover_);
      ENVOY_LOG_MISC(trace, "Attempting to reconnect to the primary gRPC source, as a connection "
                            "to it was previously established.");
      establishStreamToPrimaryIfNotConnected();
      return;
    } else if (ever_connected_to_ == ConnectedState::Failover) {
      ASSERT(!connecting_to_primary_);
      ENVOY_LOG_MISC(trace, "Attempting to reconnect to the failover gRPC source, as a connection "
                            "to it was previously established.");
      establishStreamToFailoverIfNotConnected();
      return;
    }
    // No prior connection was established, prefer the primary over the failover.
    if (connecting_to_failover_) {
      establishStreamToFailoverIfNotConnected();
    } else {
      // Either connecting to primary or connected to it, or neither.
      establishStreamToPrimaryIfNotConnected();
    }
  }

  // Returns the availability of the underlying stream.
  bool grpcStreamAvailable() const override {
    if (connectingToOrConnectedToFailover()) {
      return failover_grpc_stream_->grpcStreamAvailable();
    }
    // Either connecting/connected to the primary, or no connection was attempted.
    return primary_grpc_stream_->grpcStreamAvailable();
  }

  // Sends a message using the underlying stream.
  void sendMessage(const RequestType& request) override {
    if (connectingToOrConnectedToFailover()) {
      failover_grpc_stream_->sendMessage(request);
      return;
    }
    // Either connecting/connected to the primary, or no connection was attempted.
    primary_grpc_stream_->sendMessage(request);
  }

  // Updates the queue size of the underlying stream.
  void maybeUpdateQueueSizeStat(uint64_t size) override {
    if (connectingToOrConnectedToFailover()) {
      failover_grpc_stream_->maybeUpdateQueueSizeStat(size);
      return;
    }
    // Either connecting/connected to the primary, or no connection was attempted.
    primary_grpc_stream_->maybeUpdateQueueSizeStat(size);
  }

  // Returns true if the rate-limit allows draining.
  bool checkRateLimitAllowsDrain() override {
    if (connectingToOrConnectedToFailover()) {
      return failover_grpc_stream_->checkRateLimitAllowsDrain();
    }
    // Either connecting/connected to the primary, or no connection was attempted.
    return primary_grpc_stream_->checkRateLimitAllowsDrain();
  }

  // Returns the close status for testing purposes only.
  absl::optional<Grpc::Status::GrpcStatus> getCloseStatusForTest() {
    if (connectingToOrConnectedToFailover()) {
      return failover_grpc_stream_->getCloseStatusForTest();
    }
    ASSERT(connectingToOrConnectedToPrimary());
    return primary_grpc_stream_->getCloseStatusForTest();
  }

  // Returns the current stream for testing purposes only.
  GrpcStreamInterface<RequestType, ResponseType>& currentStreamForTest() {
    if (connectingToOrConnectedToFailover()) {
      return *failover_grpc_stream_;
    }
    ASSERT(connectingToOrConnectedToPrimary());
    return *primary_grpc_stream_;
  };

  // Retries to connect again to the primary and then (possibly) to the
  // failover. Assumes that no connection has been made or is being attempted.
  void retryConnections() {
    ASSERT(!connecting_to_primary_ && !connecting_to_failover_ &&
           (connected_to_ == ConnectedState::None));
    ENVOY_LOG(trace, "Expired timer, retrying to reconnect to the primary xDS server.");
    connecting_to_primary_ = true;
    primary_grpc_stream_->establishNewStream();
  }

private:
  // A helper class that proxies the callbacks of GrpcStreamCallbacks for the primary service.
  class PrimaryGrpcStreamCallbacks : public GrpcStreamCallbacks<ResponseType> {
  public:
    PrimaryGrpcStreamCallbacks(GrpcMuxFailover& parent) : parent_(parent) {}

    void onStreamEstablished() override {
      // Although onStreamEstablished is invoked on the the primary stream, Envoy
      // needs to wait for the first response to be received from it before
      // considering the primary source as available.
      // Calling the onStreamEstablished() callback on the GrpcMux object will
      // trigger the GrpcMux to start sending requests.
      ASSERT(parent_.connecting_to_primary_ && !parent_.connecting_to_failover_ &&
             (parent_.connected_to_ == ConnectedState::None));
      parent_.grpc_mux_callbacks_.onStreamEstablished();
    }

    void onEstablishmentFailure() override {
      // This will be called when the primary stream fails to establish a connection, or after the
      // connection was closed.
      ASSERT(parent_.connectingToOrConnectedToPrimary() &&
             !parent_.connectingToOrConnectedToFailover());
      // If there's no failover supported, this will just be a pass-through
      // callback.
      if (parent_.failover_grpc_stream_ != nullptr) {
        if (parent_.connecting_to_primary_ &&
            (parent_.ever_connected_to_ != ConnectedState::Primary)) {
          // If there are 2 consecutive failures to the primary, Envoy will try to connect to the
          // failover.
          primary_consecutive_failures_++;
          if (primary_consecutive_failures_ >= 2) {
            // The primary stream failed to establish a connection 2 times in a row.
            // Terminate the primary stream and establish a connection to the failover stream.
            ENVOY_LOG(debug, "Primary xDS stream failed to establish a connection at least 2 times "
                             "in a row. Attempting to connect to the failover stream.");
            parent_.connecting_to_primary_ = false;
            // This will close the stream and prevent the retry timer from
            // reconnecting to the primary source.
            parent_.primary_grpc_stream_->closeStream();
            parent_.grpc_mux_callbacks_.onEstablishmentFailure();
            parent_.connecting_to_failover_ = true;
            parent_.failover_grpc_stream_->establishNewStream();
            return;
          }
        }
      }
      // Pass along the failure to the GrpcMux object. Retry will be triggered
      // later by the underlying grpc stream.
      ENVOY_LOG_MISC(trace, "Not trying to connect to failover. Will try again to reconnect to the "
                            "primary (upon retry).");
      parent_.connecting_to_primary_ = true;
      parent_.connected_to_ = ConnectedState::None;
      parent_.grpc_mux_callbacks_.onEstablishmentFailure();
    }

    void onDiscoveryResponse(ResponseProtoPtr<ResponseType>&& message,
                             ControlPlaneStats& control_plane_stats) override {
      ASSERT((parent_.connectingToOrConnectedToPrimary()) &&
             !parent_.connectingToOrConnectedToFailover());
      // Received a response from the primary. The primary is now considered available (no failover
      // will be attempted).
      parent_.ever_connected_to_ = ConnectedState::Primary;
      primary_consecutive_failures_ = 0;
      parent_.connected_to_ = ConnectedState::Primary;
      parent_.connecting_to_primary_ = false;
      parent_.grpc_mux_callbacks_.onDiscoveryResponse(std::move(message), control_plane_stats);
    }

    void onWriteable() override {
      if (parent_.connectingToOrConnectedToPrimary()) {
        parent_.grpc_mux_callbacks_.onWriteable();
      }
    }

  private:
    GrpcMuxFailover& parent_;
    uint32_t primary_consecutive_failures_{0};
  };

  // A helper class that proxies the callbacks of GrpcStreamCallbacks for the failover service.
  class FailoverGrpcStreamCallbacks : public GrpcStreamCallbacks<ResponseType> {
  public:
    FailoverGrpcStreamCallbacks(GrpcMuxFailover& parent) : parent_(parent) {}

    void onStreamEstablished() override {
      // Although the failover stream is considered established, need to wait for the
      // the first response to be received before considering the failover available.
      // Calling the onStreamEstablished() callback on the GrpcMux object will
      // trigger the GrpcMux to start sending requests.
      ASSERT(parent_.connecting_to_failover_ && !parent_.connecting_to_primary_ &&
             (parent_.connected_to_ == ConnectedState::None));
      parent_.grpc_mux_callbacks_.onStreamEstablished();
    }

    void onEstablishmentFailure() override {
      // This will be called when the failover stream fails to establish a connection, or after the
      // connection was closed.
      ASSERT(parent_.connectingToOrConnectedToFailover() &&
             !parent_.connectingToOrConnectedToPrimary());
      if (parent_.ever_connected_to_ != ConnectedState::Failover) {
        ASSERT(parent_.connecting_to_failover_);
        // If Envoy never established a connecting the failover, it will try to connect to the
        // primary next.
        ENVOY_LOG(debug, "Failover xDS stream failed to establish a connection. Attempting to "
                         "connect to the primary stream.");
        parent_.connecting_to_failover_ = false;
        // This will close the stream and prevent the retry timer from
        // reconnecting to the failover source.
        parent_.failover_grpc_stream_->closeStream();
        parent_.grpc_mux_callbacks_.onEstablishmentFailure();
        // Wait for a short period of time before retrying to reconnect to the
        // primary, reducing strain on the network/servers in case of an issue.
        // TODO(adisuissa): In the future, the reconnection attempts to the
        // primary and failover sources will be decoupled, as each will use its
        // own backoff timer, and this will not be needed.
        parent_.complete_retry_timer_->enableTimer(std::chrono::milliseconds(500));
        return;
      }
      // Pass along the failure to the GrpcMux object. Retry will be triggered
      // later by the underlying grpc stream.
      ENVOY_LOG_MISC(trace, "Not trying to connect to primary. Will try again to reconnect to the "
                            "failover (upon retry).");
      parent_.connecting_to_failover_ = true;
      parent_.connected_to_ = ConnectedState::None;
      parent_.grpc_mux_callbacks_.onEstablishmentFailure();
    }

    void onDiscoveryResponse(ResponseProtoPtr<ResponseType>&& message,
                             ControlPlaneStats& control_plane_stats) override {
      ASSERT(parent_.connectingToOrConnectedToFailover() &&
             !parent_.connectingToOrConnectedToPrimary());
      // Received a response from the failover. The failover is now considered available (no going
      // back to the primary will be attempted).
      // TODO(adisuissa): This will be modified in the future, when allowing the primary to always
      // be preferred over the failover.
      parent_.ever_connected_to_ = ConnectedState::Failover;
      parent_.connected_to_ = ConnectedState::Failover;
      parent_.connecting_to_failover_ = false;
      parent_.grpc_mux_callbacks_.onDiscoveryResponse(std::move(message), control_plane_stats);
    }

    void onWriteable() override {
      if (parent_.connectingToOrConnectedToFailover()) {
        parent_.grpc_mux_callbacks_.onWriteable();
      }
    }

  private:
    GrpcMuxFailover& parent_;
  };

  // Returns true iff the state is connecting to primary or connected to it.
  bool connectingToOrConnectedToPrimary() const {
    return connecting_to_primary_ || (connected_to_ == ConnectedState::Primary);
  }

  // Returns true iff the state is connecting to failover or connected to it.
  bool connectingToOrConnectedToFailover() const {
    return connecting_to_failover_ || (connected_to_ == ConnectedState::Failover);
  }

  // Establishes a new stream to the primary source if not connected to it.
  void establishStreamToPrimaryIfNotConnected() {
    if (connected_to_ != ConnectedState::Primary) {
      ASSERT(connected_to_ == ConnectedState::None);
      connecting_to_primary_ = true;
      primary_grpc_stream_->establishNewStream();
    }
  }

  // Establishes a new stream to the failover source if not connected to it.
  void establishStreamToFailoverIfNotConnected() {
    if (connected_to_ != ConnectedState::Failover) {
      ASSERT(connected_to_ == ConnectedState::None);
      connecting_to_failover_ = true;
      failover_grpc_stream_->establishNewStream();
    }
  }

  // The following method overrides are to allow GrpcMuxFailover to extend the
  // GrpcStreamInterface. Once envoy.restart_features.xds_failover_support is deprecated,
  // the class will no longer need to extend the interface, and these can be removed.
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override { PANIC("not implemented"); }
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override { PANIC("not implemented"); }
  void onReceiveMessage(std::unique_ptr<ResponseType>&&) override { PANIC("not implemented"); }
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {
    PANIC("not implemented");
  }
  void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override {
    PANIC("not implemented");
  }
  void closeStream() override { PANIC("not implemented"); }

  // The stream callbacks that will be invoked on the GrpcMux object, to notify
  // about the state of the underlying primary/failover stream.
  GrpcStreamCallbacks<ResponseType>& grpc_mux_callbacks_;
  // The callbacks that will be invoked by the primary stream.
  PrimaryGrpcStreamCallbacks primary_callbacks_;
  // The stream to the primary source.
  GrpcStreamInterfacePtr<RequestType, ResponseType> primary_grpc_stream_;
  // The callbacks that will be invoked by the failover stream.
  std::unique_ptr<FailoverGrpcStreamCallbacks> failover_callbacks_;
  // The stream to the failover source.
  GrpcStreamInterfacePtr<RequestType, ResponseType> failover_grpc_stream_;

  // A timer that allows waiting for some period of time before trying to
  // connect again after both primary and failover attempts failed. Only
  // initialized when failover is supported.
  Event::TimerPtr complete_retry_timer_{nullptr};

  enum class ConnectedState { None, Primary, Failover };

  // Flags to keep track of the state of connections to primary/failover.
  // All initialized to false/None, as there is no connection process during
  // initialization.
  // The object starts with all the connecting flags set to false, and
  // connected_to to None. Once a new stream is attempted,
  // connecting_to_primary_ will become true, until a response will be received
  // from the primary (connected_to_ will become Primary), or a failure
  // to establish a connection to the primary occurs. In the latter case, if
  // Envoy attempts to reconnect to the primary, connecting_to_primary_ will
  // stay true, but if it attempts to connect to the failover, connecting_to_primary_
  // will be set to false, and connecting_to_failover_ will be true.
  // The values of connecting_to_failover_ and connected_to_ set to Failover will be
  // determined similar to the primary variants.
  // Note that while Envoy can only be connected to a single source (mutually
  // exclusive), it can attempt connecting to more than one source at a time.
  bool connecting_to_primary_{false};
  bool connecting_to_failover_{false};
  ConnectedState connected_to_;

  // A flag that keeps track of whether Envoy successfully connected to either the
  // primary or failover source. Envoy successfully connected to a source once
  // it receives a response from it.
  ConnectedState ever_connected_to_;
};

} // namespace Config
} // namespace Envoy
