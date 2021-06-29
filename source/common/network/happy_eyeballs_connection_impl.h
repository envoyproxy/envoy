#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/scope_tracker.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "absl/types/optional.h"

#include "source/common/network/connection_impl.h"

namespace Envoy {
namespace Network {

/**
 * Implementation of Network::Connection, Network::FilterManagerConnection and
 * Envoy::ScopeTrackedObject.
 */
class HappyEyeballsConnectionImpl : public ClientConnection {
public:
  HappyEyeballsConnectionImpl(Event::Dispatcher& dispatcher,
                              Network::Address::InstanceConstSharedPtr address,
                              Network::Address::InstanceConstSharedPtr source_address,
                              Network::TransportSocketFactory& socket_factory,
                              Network::TransportSocketOptionsSharedPtr transport_socket_options,
                              const Network::ConnectionSocket::OptionsSharedPtr options);

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
  void hashKey(std::vector<uint8_t>& hash) const override;
  void setConnectionStats(const ConnectionStats& stats) override;
  void setDelayedCloseTimeout(std::chrono::milliseconds timeout) override;

  // ScopeTrackedObject
  void dumpState(std::ostream& os, int indent_level) const override;

private:
  class ConnectionCallbacksWrapper : public ConnectionCallbacks {
   public:
    ConnectionCallbacksWrapper(HappyEyeballsConnectionImpl& parent,
                               ClientConnection& connection)
        : parent_(parent), connection_(connection) {}

    void onEvent(ConnectionEvent event) override {
      parent_.onEvent(event, this);
    }

    void onAboveWriteBufferHighWatermark() override {
      parent_.onAboveWriteBufferHighWatermark(this);
    }

    void onBelowWriteBufferLowWatermark() override {
      parent_.onBelowWriteBufferLowWatermark(this);
    }

    ClientConnection& connection() { return connection_; }

   private:
    HappyEyeballsConnectionImpl& parent_;
    ClientConnection& connection_;
  };

  std::unique_ptr<ClientConnection> createConnection();

  void onEvent(ConnectionEvent event, ConnectionCallbacksWrapper* wrapper);

  void onAboveWriteBufferHighWatermark(ConnectionCallbacksWrapper* wrapper);

  void onBelowWriteBufferLowWatermark(ConnectionCallbacksWrapper* wrapper);

  void onWriteBufferHighWatermark();
  void onWriteBufferLowWatermark();

  struct State {
    bool detectEarlyCloseWhenReadDisabled_ = false;
    bool noDelay_ = false;
    Buffer::InstancePtr write_buffer_;
    bool end_stream_ = false;
  };

  std::unique_ptr<ClientConnection> connection_;
  std::vector<ConnectionCallbacks*> cbs_;
  std::vector<ReadFilterSharedPtr> read_filters_;
  State state_;
  bool connect_finished_ = false;
  std::unique_ptr<ConnectionCallbacksWrapper> callbacks_wrapper_;
  std::unique_ptr<ClientConnection> pending_connection_;

  Event::Dispatcher& dispatcher_;
  Network::Address::InstanceConstSharedPtr address_;
  Network::Address::InstanceConstSharedPtr source_address_;
  Network::TransportSocketFactory& socket_factory_;
  Network::TransportSocketOptionsSharedPtr transport_socket_options_;
  const Network::ConnectionSocket::OptionsSharedPtr options_;
};

} // namespace Network
} // namespace Envoy
