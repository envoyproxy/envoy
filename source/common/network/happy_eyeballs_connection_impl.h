#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/scope_tracker.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "source/common/network/connection_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Network {

/**
 * Implementation of ClientConnection which transparently attempts connections to
 * multiple different IP addresses, and use the first connection that succeeds.
 * See the Happy Eyeballs RFC at https://datatracker.ietf.org/doc/html/rfc6555
 * TODO(RyanTheOptimist): Implement the Happy Eyeballs address sorting algorithm
 * either in the class or in the resolution code.
 */
class HappyEyeballsConnectionImpl : public ClientConnection {
public:
  HappyEyeballsConnectionImpl(Event::Dispatcher& dispatcher,
                              const std::vector<Address::InstanceConstSharedPtr>& address_list,
                              Address::InstanceConstSharedPtr source_address,
                              TransportSocketFactory& socket_factory,
                              TransportSocketOptionsConstSharedPtr transport_socket_options,
                              const ConnectionSocket::OptionsSharedPtr options);

  ~HappyEyeballsConnectionImpl() override;

  // Network::ClientConnection
  void connect() override;

  // Network::FilterManager
  void addWriteFilter(WriteFilterSharedPtr filter) override;
  void addFilter(FilterSharedPtr filter) override;
  void addReadFilter(ReadFilterSharedPtr filter) override;
  void removeReadFilter(ReadFilterSharedPtr filter) override;
  bool initializeReadFilters() override;

  // Network::Connection
  void addBytesSentCallback(BytesSentCb cb) override;
  void enableHalfClose(bool enabled) override;
  bool isHalfCloseEnabled() override;
  std::string nextProtocol() const override;
  void noDelay(bool enable) override;
  void readDisable(bool disable) override;
  void detectEarlyCloseWhenReadDisabled(bool value) override;
  bool readEnabled() const override;
  const SocketAddressProvider& addressProvider() const override;
  SocketAddressProviderSharedPtr addressProviderSharedPtr() const override;
  absl::optional<UnixDomainSocketPeerCredentials> unixSocketPeerCredentials() const override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override;
  State state() const override;
  bool connecting() const override;
  void write(Buffer::Instance& data, bool end_stream) override;
  void setBufferLimits(uint32_t limit) override;
  uint32_t bufferLimit() const override;
  bool aboveHighWatermark() const override;
  const ConnectionSocket::OptionsSharedPtr& socketOptions() const override;
  absl::string_view requestedServerName() const override;
  StreamInfo::StreamInfo& streamInfo() override;
  const StreamInfo::StreamInfo& streamInfo() const override;
  absl::string_view transportFailureReason() const override;
  bool startSecureTransport() override;
  absl::optional<std::chrono::milliseconds> lastRoundTripTime() const override;
  void addConnectionCallbacks(ConnectionCallbacks& cb) override;
  void removeConnectionCallbacks(ConnectionCallbacks& cb) override;
  void close(ConnectionCloseType type) override;
  Event::Dispatcher& dispatcher() override;
  uint64_t id() const override;
  void hashKey(std::vector<uint8_t>& hash_key) const override;
  void setConnectionStats(const ConnectionStats& stats) override;
  void setDelayedCloseTimeout(std::chrono::milliseconds timeout) override;

  // ScopeTrackedObject
  void dumpState(std::ostream& os, int indent_level) const override;

private:
  // ConnectionCallbacks which will be set on an ClientConnection which
  // sends connection events back to the HappyEyeballsConnectionImpl.
  class ConnectionCallbacksWrapper : public ConnectionCallbacks {
  public:
    ConnectionCallbacksWrapper(HappyEyeballsConnectionImpl& parent, ClientConnection& connection)
        : parent_(parent), connection_(connection) {}

    void onEvent(ConnectionEvent event) override { parent_.onEvent(event, this); }

    void onAboveWriteBufferHighWatermark() override {
      parent_.onAboveWriteBufferHighWatermark(this);
    }

    void onBelowWriteBufferLowWatermark() override { parent_.onBelowWriteBufferLowWatermark(this); }

    // Not needed? interesting.
    ClientConnection& connection() { return connection_; }

  private:
    HappyEyeballsConnectionImpl& parent_;
    ClientConnection& connection_;
  };

  ClientConnectionPtr createNextConnection();
  void tryAnotherConnection();
  void maybeScheduleNextAttempt();

  void onEvent(ConnectionEvent event, ConnectionCallbacksWrapper* wrapper);

  void onAboveWriteBufferHighWatermark(ConnectionCallbacksWrapper* wrapper);

  void onBelowWriteBufferLowWatermark(ConnectionCallbacksWrapper* wrapper);

  void onWriteBufferHighWatermark();

  void cleanupWrapperAndConnection(ConnectionCallbacksWrapper* wrapper);

  // State which needs to be applied to every connection attempt.
  struct PerConnectionState {
    absl::optional<bool> detect_early_close_when_read_disabled_;
    absl::optional<bool> no_delay_;
    absl::optional<bool> enable_half_close_;
    OptRef<const ConnectionStats> connection_stats_;
    absl::optional<uint32_t> buffer_limits_;
    absl::optional<bool> start_secure_transport_;
    absl::optional<std::chrono::milliseconds> delayed_close_timeout_;
  };

  // State which needs to be saved and applied only to the final connection
  // attempt.
  struct PostConnectState {
    std::vector<ConnectionCallbacks*> connection_callbacks_;
    std::vector<Connection::BytesSentCb> bytes_sent_callbacks_;
    std::vector<ReadFilterSharedPtr> read_filters_;
    std::vector<WriteFilterSharedPtr> write_filters_;
    std::vector<FilterSharedPtr> filters_;
    absl::optional<Buffer::InstancePtr> write_buffer_;
    absl::optional<int> read_disable_count_;
    absl::optional<bool> end_stream_;
    absl::optional<bool> initialize_read_filters_;
  };

  // ID for this connection which is distinct from the ID of the underlying connections.
  const uint64_t id_;

  Event::Dispatcher& dispatcher_;
  const std::vector<Address::InstanceConstSharedPtr>& address_list_;
  size_t next_address_ = 0;
  Address::InstanceConstSharedPtr source_address_;
  TransportSocketFactory& socket_factory_;
  TransportSocketOptionsConstSharedPtr transport_socket_options_;
  const ConnectionSocket::OptionsSharedPtr options_;

  // Set of active connections.
  std::vector<ClientConnectionPtr> connections_;
  std::vector<std::unique_ptr<ConnectionCallbacksWrapper>> callbacks_wrappers_;

  // True when connect() has finished, either success or failure.
  bool connect_finished_ = false;
  Event::TimerPtr next_attempt_timer_;

  bool above_write_high_water_mark_ = false;
  PerConnectionState per_connection_state_;
  PostConnectState post_connect_state_;
};

} // namespace Network
} // namespace Envoy
