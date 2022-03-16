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
 * multiple different IP addresses, and uses the first connection that succeeds.
 * After a connection is established, all methods simply delegate to the
 * underlying connection. However, before the connection is established
 * their behavior depends on their semantics. For anything which can result
 * in up-call (e.g. filter registration) or which must only happen once (e.g.
 * writing data) the context is saved in until the connection completes, at
 * which point they are replayed to the underlying connection. For simple methods
 * they are applied to each open connection and applied when creating new ones.
 *
 * See the Happy Eyeballs RFC at https://datatracker.ietf.org/doc/html/rfc6555
 * TODO(RyanTheOptimist): Implement the Happy Eyeballs address sorting algorithm
 * either in the class or in the resolution code.
 */
class HappyEyeballsConnectionImpl : public ClientConnection,
                                    Logger::Loggable<Logger::Id::happy_eyeballs> {
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

  // Methods which defer action until the final connection has been determined.
  void addWriteFilter(WriteFilterSharedPtr filter) override;
  void addFilter(FilterSharedPtr filter) override;
  void addReadFilter(ReadFilterSharedPtr filter) override;
  void removeReadFilter(ReadFilterSharedPtr filter) override;
  bool initializeReadFilters() override;
  void addBytesSentCallback(BytesSentCb cb) override;
  void write(Buffer::Instance& data, bool end_stream) override;
  void addConnectionCallbacks(ConnectionCallbacks& cb) override;
  void removeConnectionCallbacks(ConnectionCallbacks& cb) override;

  // Methods which are applied to each connection attempt.
  void enableHalfClose(bool enabled) override;
  void noDelay(bool enable) override;
  void readDisable(bool disable) override;
  void detectEarlyCloseWhenReadDisabled(bool value) override;
  void setConnectionStats(const ConnectionStats& stats) override;
  void setDelayedCloseTimeout(std::chrono::milliseconds timeout) override;
  void setBufferLimits(uint32_t limit) override;
  bool startSecureTransport() override;
  absl::optional<std::chrono::milliseconds> lastRoundTripTime() const override;
  void configureInitialCongestionWindow(uint64_t, std::chrono::microseconds) override {}
  absl::optional<uint64_t> congestionWindowInBytes() const override;

  // Simple getters which always delegate to the first connection in connections_.
  bool isHalfCloseEnabled() override;
  std::string nextProtocol() const override;
  // Note, this might change before connect finishes.
  ConnectionInfoSetter& connectionInfoSetter() override;
  // Note, this might change before connect finishes.
  const ConnectionInfoProvider& connectionInfoProvider() const override;
  // Note, this might change before connect finishes.
  ConnectionInfoProviderSharedPtr connectionInfoProviderSharedPtr() const override;
  // Note, this might change before connect finishes.
  absl::optional<UnixDomainSocketPeerCredentials> unixSocketPeerCredentials() const override;
  // Note, this might change before connect finishes.
  Ssl::ConnectionInfoConstSharedPtr ssl() const override;
  State state() const override;
  bool connecting() const override;
  uint32_t bufferLimit() const override;
  const ConnectionSocket::OptionsSharedPtr& socketOptions() const override;
  absl::string_view requestedServerName() const override;
  StreamInfo::StreamInfo& streamInfo() override;
  const StreamInfo::StreamInfo& streamInfo() const override;
  absl::string_view transportFailureReason() const override;

  // Methods implemented largely by this class itself.
  uint64_t id() const override;
  Event::Dispatcher& dispatcher() override;
  void close(ConnectionCloseType type) override;
  bool readEnabled() const override;
  bool aboveHighWatermark() const override;
  void hashKey(std::vector<uint8_t>& hash_key) const override;
  void dumpState(std::ostream& os, int indent_level) const override;

  // Returns a new vector containing the contents of |address_list| sorted
  // with address families interleaved, as per Section 4 of RFC 8305, Happy
  // Eyeballs v2. It is assumed that the list must already be sorted as per
  // Section 6 of RFC6724, which happens in the DNS implementations (ares_getaddrinfo()
  // and Apple DNS).
  static std::vector<Address::InstanceConstSharedPtr>
  sortAddresses(const std::vector<Address::InstanceConstSharedPtr>& address_list);

private:
  // ConnectionCallbacks which will be set on an ClientConnection which
  // sends connection events back to the HappyEyeballsConnectionImpl.
  class ConnectionCallbacksWrapper : public ConnectionCallbacks {
  public:
    ConnectionCallbacksWrapper(HappyEyeballsConnectionImpl& parent, ClientConnection& connection)
        : parent_(parent), connection_(connection) {}

    void onEvent(ConnectionEvent event) override { parent_.onEvent(event, this); }

    void onAboveWriteBufferHighWatermark() override {
      // No data will be written to the connection while the wrapper is associated with it,
      // so the write buffer should never hit the high watermark.
      IS_ENVOY_BUG("Unexpected data written to happy eyeballs connection");
    }

    void onBelowWriteBufferLowWatermark() override {
      // No data will be written to the connection while the wrapper is associated with it,
      // so the write buffer should never hit the high watermark.
      IS_ENVOY_BUG("Unexpected data drained from happy eyeballs connection");
    }

    ClientConnection& connection() { return connection_; }

  private:
    HappyEyeballsConnectionImpl& parent_;
    ClientConnection& connection_;
  };

  // Creates a connection to the next address in address_list_ and applies
  // any settings from per_connection_state_ to the newly created connection.
  ClientConnectionPtr createNextConnection();

  // Create a new connection, connects it and scheduled a timer to start another
  // connection attempt if there are more addresses to connect to.
  void tryAnotherConnection();

  // Schedules another connection attempt if there are mode address to connect to.
  void maybeScheduleNextAttempt();

  // Called by the wrapper when the wrapped connection raises the specified event.
  void onEvent(ConnectionEvent event, ConnectionCallbacksWrapper* wrapper);

  // Called to bind the final connection. All other connections will be closed, and
  // and deferred operations will be replayed.
  void setUpFinalConnection(ConnectionEvent event, ConnectionCallbacksWrapper* wrapper);

  // Called by the write buffer containing pending writes if it goes below the
  // low water mark.
  void onWriteBufferLowWatermark();

  // Called by the write buffer containing pending writes if it goes above the
  // high water mark.
  void onWriteBufferHighWatermark();

  // Cleans up all state for the connection associated with wrapper. Called when the
  // connection is no longer needed.
  void cleanupWrapperAndConnection(ConnectionCallbacksWrapper* wrapper);

  // State which needs to be applied to every connection attempt.
  struct PerConnectionState {
    absl::optional<bool> detect_early_close_when_read_disabled_;
    absl::optional<bool> no_delay_;
    absl::optional<bool> enable_half_close_;
    std::unique_ptr<ConnectionStats> connection_stats_;
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

  // State which is needed to construct a new connection.
  struct ConnectionConstructionState {
    Address::InstanceConstSharedPtr source_address_;
    TransportSocketFactory& socket_factory_;
    TransportSocketOptionsConstSharedPtr transport_socket_options_;
    const ConnectionSocket::OptionsSharedPtr options_;
  };

  // ID for this connection which is distinct from the ID of the underlying connections.
  const uint64_t id_;

  Event::Dispatcher& dispatcher_;

  // List of addresses to attempt to connect to.
  const std::vector<Address::InstanceConstSharedPtr> address_list_;
  // Index of the next address to use.
  size_t next_address_ = 0;

  ConnectionConstructionState connection_construction_state_;
  PerConnectionState per_connection_state_;
  PostConnectState post_connect_state_;

  // Set of active connections.
  std::vector<ClientConnectionPtr> connections_;
  std::vector<std::unique_ptr<ConnectionCallbacksWrapper>> callbacks_wrappers_;

  // True when connect() has finished, either success or failure.
  bool connect_finished_ = false;
  Event::TimerPtr next_attempt_timer_;
};

} // namespace Network
} // namespace Envoy
