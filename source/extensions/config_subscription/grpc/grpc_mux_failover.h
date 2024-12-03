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
 * To simplify the state-machine, Envoy can be in one of the mutually exclusive states:
 *   ConnectingToPrimary - attempting to connect to the primary source.
 *   ConnectedToPrimary - after receiving a response from the primary source.
 *   ConnectingToFailover - attempting to connect to the failover source.
 *   ConnectedToFailover - after receiving a response from the failover source.
 *   None - not attempting to connect or connected to any source (e.g., upon  initialization).
 *
 * The GrpcMuxFailover attempts to establish a connection to the primary source. Once a response is
 * received from the primary source it will be considered available, and the failover will not be
 * used. Any future reconnection attempts will be to the primary source only.
 * However, if no response is received from the primary source, and accessing the primary has
 * failed 2 times in a row, the GrpcMuxFailover will attempt to establish a connection to the
 * failover source. Envoy will keep alternating between the primary and failover sources attempting
 * to connect to one of them. If a response from the failover source is received, it will be the
 * source of configuration until the connection is closed. In case the failover connection is
 * closed, Envoy will attempt to connect to the primary, before retrying to connect to the failover
 * source. If the failover source is unavailable or a connection to it is closed, the
 * GrpcMuxFailover will alternate between attempts to reconnect to the primary source and the
 * failover source.
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
        connection_state_(ConnectionState::None), ever_connected_to_primary_(false),
        previously_connected_to_(ConnectedTo::None) {
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
    // This method may be called multiple times, even if the primary/failover stream
    // is already established or in the process of being established.
    if (complete_retry_timer_) {
      complete_retry_timer_->disableTimer();
    }
    // If already connected to one of the source, return.
    if (connection_state_ == ConnectionState::ConnectedToPrimary ||
        connection_state_ == ConnectionState::ConnectedToFailover) {
      ENVOY_LOG_MISC(trace,
                     "Already connected to an xDS server, skipping establishNewStream() call");
      return;
    }
    if (!Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.xds_failover_to_primary_enabled")) {
      // Allow stickiness, so if Envoy was ever connected to the primary source only
      // retry to reconnect to the primary source, If Envoy was ever connected to the
      // failover source then only retry to reconnect to the failover source.
      if (previously_connected_to_ == ConnectedTo::Primary) {
        ENVOY_LOG_MISC(
            info, "Previously connected to the primary xDS source, attempting to reconnect to it");
        connection_state_ = ConnectionState::ConnectingToPrimary;
        primary_grpc_stream_->establishNewStream();
        return;
      } else if (previously_connected_to_ == ConnectedTo::Failover) {
        ENVOY_LOG_MISC(
            info, "Previously connected to the failover xDS source, attempting to reconnect to it");
        connection_state_ = ConnectionState::ConnectingToFailover;
        failover_grpc_stream_->establishNewStream();
        return;
      }
    }
    // connection_state_ is either None, ConnectingToPrimary or
    // ConnectingToFailover. In the first 2 cases try to connect to the primary
    // (preferring the primary in the case of None), and in the third case
    // try to connect to the failover.
    // Note that if a connection to the primary source was ever successful, the
    // failover manager will keep setting connection_state_ to either None or
    // ConnectingToPrimary, which ensures that only the primary stream will be
    // established.
    if (connection_state_ == ConnectionState::ConnectingToFailover) {
      ASSERT(!ever_connected_to_primary_);
      failover_grpc_stream_->establishNewStream();
    } else {
      ASSERT(connection_state_ == ConnectionState::None ||
             connection_state_ == ConnectionState::ConnectingToPrimary);
      connection_state_ = ConnectionState::ConnectingToPrimary;
      primary_grpc_stream_->establishNewStream();
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
      ASSERT(!ever_connected_to_primary_);
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
    ASSERT(connection_state_ == ConnectionState::None);
    ENVOY_LOG(trace, "Expired timer, retrying to reconnect to the primary xDS server.");
    connection_state_ = ConnectionState::ConnectingToPrimary;
    primary_grpc_stream_->establishNewStream();
  }

private:
  friend class GrpcMuxFailoverTest;

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
      ASSERT(parent_.connection_state_ == ConnectionState::ConnectingToPrimary);
      parent_.grpc_mux_callbacks_.onStreamEstablished();
    }

    void onEstablishmentFailure(bool) override {
      // This will be called when the primary stream fails to establish a connection, or after the
      // connection was closed.
      ASSERT(parent_.connectingToOrConnectedToPrimary());
      // If there's no failover supported, this will just be a pass-through
      // callback.
      if (parent_.failover_grpc_stream_ != nullptr) {
        if (parent_.connection_state_ == ConnectionState::ConnectingToPrimary &&
            !parent_.ever_connected_to_primary_) {
          // If there are 2 consecutive failures to the primary, Envoy will try to connect to the
          // failover.
          primary_consecutive_failures_++;
          if (primary_consecutive_failures_ >= 2) {
            // The primary stream failed to establish a connection 2 times in a row.
            // Terminate the primary stream and establish a connection to the failover stream.
            ENVOY_LOG(debug, "Primary xDS stream failed to establish a connection at least 2 times "
                             "in a row. Attempting to connect to the failover stream.");
            // This will close the stream and prevent the retry timer from
            // reconnecting to the primary source.
            parent_.primary_grpc_stream_->closeStream();
            // Next attempt will be to the failover, set the value that
            // determines whether to set initial_resource_versions or not.
            parent_.grpc_mux_callbacks_.onEstablishmentFailure(parent_.previously_connected_to_ ==
                                                               ConnectedTo::Failover);
            parent_.connection_state_ = ConnectionState::ConnectingToFailover;
            parent_.failover_grpc_stream_->establishNewStream();
            return;
          }
        }
      }
      // Pass along the failure to the GrpcMux object. Retry will be triggered
      // later by the underlying grpc stream.
      ENVOY_LOG_MISC(trace, "Not trying to connect to failover. Will try again to reconnect to the "
                            "primary (upon retry).");
      parent_.connection_state_ = ConnectionState::ConnectingToPrimary;
      // Next attempt will be to the primary, set the value that
      // determines whether to set initial_resource_versions or not.
      parent_.grpc_mux_callbacks_.onEstablishmentFailure(parent_.previously_connected_to_ ==
                                                         ConnectedTo::Primary);
    }

    void onDiscoveryResponse(ResponseProtoPtr<ResponseType>&& message,
                             ControlPlaneStats& control_plane_stats) override {
      ASSERT((parent_.connectingToOrConnectedToPrimary()) &&
             !parent_.connectingToOrConnectedToFailover());
      // Received a response from the primary. The primary is now considered available (no failover
      // will be attempted).
      parent_.ever_connected_to_primary_ = true;
      primary_consecutive_failures_ = 0;
      parent_.connection_state_ = ConnectionState::ConnectedToPrimary;
      parent_.previously_connected_to_ = ConnectedTo::Primary;
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
      ASSERT(parent_.connection_state_ == ConnectionState::ConnectingToFailover);
      ASSERT(!parent_.ever_connected_to_primary_);
      parent_.grpc_mux_callbacks_.onStreamEstablished();
    }

    void onEstablishmentFailure(bool) override {
      // This will be called when the failover stream fails to establish a connection, or after the
      // connection was closed.
      ASSERT(parent_.connectingToOrConnectedToFailover());
      if (!Runtime::runtimeFeatureEnabled(
              "envoy.reloadable_features.xds_failover_to_primary_enabled")) {
        // If previously Envoy was connected to the failover, keep using that.
        // Otherwise let the retry mechanism try to access the primary (similar
        // to if the runtime flag was not set).
        if (parent_.previously_connected_to_ == ConnectedTo::Failover) {
          ENVOY_LOG(debug,
                    "Failover xDS stream disconnected (either after establishing a connection or "
                    "before). Attempting to reconnect to Failover because Envoy successfully "
                    "connected to it previously.");
          // Not closing the failover stream, allows it to use its retry timer
          // to reconnect to the failover source.
          // Next attempt will be to the failover after Envoy was already
          // connected to it. Allowing to send the initial_resource_versions on reconnect.
          parent_.grpc_mux_callbacks_.onEstablishmentFailure(true);
          parent_.connection_state_ = ConnectionState::ConnectingToFailover;
          return;
        }
      }
      // Either this was an intentional disconnection from the failover source,
      // or unintentional. Either way, try to connect to the primary next.
      ENVOY_LOG(debug,
                "Failover xDS stream disconnected (either after establishing a connection or "
                "before). Attempting to connect to the primary stream.");

      // This will close the stream and prevent the retry timer from
      // reconnecting to the failover source. The next attempt will be to the
      // primary source.
      parent_.failover_grpc_stream_->closeStream();
      // Next attempt will be to the primary, set the value that
      // determines whether to set initial_resource_versions or not.
      parent_.grpc_mux_callbacks_.onEstablishmentFailure(parent_.previously_connected_to_ ==
                                                         ConnectedTo::Primary);
      // Setting the connection state to None, and when the retry timer will
      // expire, Envoy will try to connect to the primary source.
      parent_.connection_state_ = ConnectionState::None;
      // Wait for a short period of time before retrying to reconnect to the
      // primary, reducing strain on the network/servers in case of an issue.
      // TODO(adisuissa): need to use the primary source's retry timer here, to wait
      // for the next time to connect to the primary. This requires a refactor
      // of the retry timer and moving it from the grpc_stream to here.
      parent_.complete_retry_timer_->enableTimer(std::chrono::milliseconds(500));
    }

    void onDiscoveryResponse(ResponseProtoPtr<ResponseType>&& message,
                             ControlPlaneStats& control_plane_stats) override {
      ASSERT(parent_.connectingToOrConnectedToFailover());
      ASSERT(!parent_.ever_connected_to_primary_);
      // Received a response from the failover. The failover is now considered available (no going
      // back to the primary will be attempted).
      parent_.connection_state_ = ConnectionState::ConnectedToFailover;
      parent_.previously_connected_to_ = ConnectedTo::Failover;
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
    return connection_state_ == ConnectionState::ConnectingToPrimary ||
           connection_state_ == ConnectionState::ConnectedToPrimary;
  }

  // Returns true iff the state is connecting to failover or connected to it.
  bool connectingToOrConnectedToFailover() const {
    return connection_state_ == ConnectionState::ConnectingToFailover ||
           connection_state_ == ConnectionState::ConnectedToFailover;
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
  void closeStream() override {
    if (connectingToOrConnectedToPrimary()) {
      ENVOY_LOG_MISC(debug, "Intentionally closing the primary gRPC stream");
      primary_grpc_stream_->closeStream();
    } else if (connectingToOrConnectedToFailover()) {
      ENVOY_LOG_MISC(debug, "Intentionally closing the failover gRPC stream");
      failover_grpc_stream_->closeStream();
    }
  }

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

  enum class ConnectionState {
    None,
    ConnectingToPrimary,
    ConnectedToPrimary,
    ConnectingToFailover,
    ConnectedToFailover
  };

  // Flags to keep track of the state of connections to primary/failover.
  // The object starts with all the connecting_to and connected_to flags set
  // to None.
  // Once a new stream is attempted, connecting_to_ will become Primary, until
  // a response will be received from the primary (connected_to_ will be set
  // to Primary), or a failure to establish a connection to the primary occurs.
  // In the latter case, if Envoy attempts to reconnect to the primary,
  // connecting_to_ will stay Primary, but if it attempts to connect to the failover,
  // connecting_to_ will be set to Failover.
  // If Envoy successfully connects to the failover, connected_to_ will be set
  // to Failover.
  // Note that while Envoy can only be connected to a single source (mutually
  // exclusive), it can attempt connecting to more than one source at a time.
  ConnectionState connection_state_;

  // A flag that keeps track of whether Envoy successfully connected to the
  // primary source. Envoy is considered successfully connected to a source
  // once it receives a response from it.
  bool ever_connected_to_primary_{false};

  enum class ConnectedTo { None, Primary, Failover };
  // Used to track the most recent source that Envoy was connected to.
  ConnectedTo previously_connected_to_;
};

} // namespace Config
} // namespace Envoy
