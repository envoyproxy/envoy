#pragma once

#include <atomic>
#include <list>
#include <ostream>

#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"

#include "source/common/network/filter_manager_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Network {

class MockConnectionCallbacks : public ConnectionCallbacks {
public:
  MockConnectionCallbacks();
  ~MockConnectionCallbacks() override;

  // Network::ConnectionCallbacks
  MOCK_METHOD(void, onEvent, (Network::ConnectionEvent event), (override));
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, (), (override));
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, (), (override));
  MOCK_METHOD(void, onDrain, (Network::ConnectionDrainEvent info), (override));
};

class MockConnectionBase {
public:
  void raiseEvent(Network::ConnectionEvent event);
  // Fan out onDrain(event) to all registered ConnectionCallbacks, mirroring the real connection's
  // Connection::onDrain(). Used to exercise connection-level drain in tests.
  void raiseConnectionDrain(Network::ConnectionDrainEvent event);
  void raiseBytesSentCallbacks(uint64_t num_bytes);
  void runHighWatermarkCallbacks();
  void runLowWatermarkCallbacks();

  static std::atomic<uint64_t> next_id_;

  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  std::list<Network::ConnectionCallbacks*> callbacks_;
  std::list<Network::Connection::BytesSentCb> bytes_sent_callbacks_;
  uint64_t id_{next_id_++};
  bool read_enabled_{true};
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::string local_close_reason_{"unset_local_close_reason"};
  Connection::State state_{Connection::State::Open};
  StreamInfo::DetectedCloseType detected_close_type_{StreamInfo::DetectedCloseType::Normal};
};

#define DEFINE_MOCK_CONNECTION_MOCK_METHODS                                                        \
  /* Network::Connection */                                                                        \
  MOCK_METHOD(void, addConnectionCallbacks, (ConnectionCallbacks & cb), (override));               \
  MOCK_METHOD(void, removeConnectionCallbacks, (ConnectionCallbacks & cb), (override));            \
  MOCK_METHOD(void, onDrain, (ConnectionDrainEvent info), (override));                             \
  MOCK_METHOD(void, addBytesSentCallback, (BytesSentCb cb), (override));                           \
  MOCK_METHOD(void, addWriteFilter, (WriteFilterSharedPtr filter), (override));                    \
  MOCK_METHOD(void, addFilter, (FilterSharedPtr filter), (override));                              \
  MOCK_METHOD(void, addReadFilter, (ReadFilterSharedPtr filter), (override));                      \
  MOCK_METHOD(void, addAccessLogHandler, (AccessLog::InstanceSharedPtr handler), (override));      \
  MOCK_METHOD(void, removeReadFilter, (ReadFilterSharedPtr filter), (override));                   \
  MOCK_METHOD(void, enableHalfClose, (bool enabled), (override));                                  \
  MOCK_METHOD(bool, isHalfCloseEnabled, (), (const, override));                                    \
  MOCK_METHOD(void, close, (ConnectionCloseType type), (override));                                \
  MOCK_METHOD(void, close, (ConnectionCloseType type, absl::string_view details), (override));     \
  MOCK_METHOD(StreamInfo::DetectedCloseType, detectedCloseType, (), (const, override));            \
  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (const, override));                              \
  MOCK_METHOD(uint64_t, id, (), (const, override));                                                \
  MOCK_METHOD(void, hashKey, (std::vector<uint8_t>&), (const, override));                          \
  MOCK_METHOD(bool, initializeReadFilters, (), (override));                                        \
  MOCK_METHOD(std::string, nextProtocol, (), (const, override));                                   \
  MOCK_METHOD(void, noDelay, (bool enable), (override));                                           \
  MOCK_METHOD(ReadDisableStatus, readDisable, (bool disable), (override));                         \
  MOCK_METHOD(void, detectEarlyCloseWhenReadDisabled, (bool), (override));                         \
  MOCK_METHOD(bool, readEnabled, (), (const, override));                                           \
  MOCK_METHOD(ConnectionInfoSetter&, connectionInfoSetter, (), (override));                        \
  MOCK_METHOD(const ConnectionInfoProvider&, connectionInfoProvider, (), (const, override));       \
  MOCK_METHOD(ConnectionInfoProviderSharedPtr, connectionInfoProviderSharedPtr, (),                \
              (const, override));                                                                  \
  MOCK_METHOD(std::optional<Connection::UnixDomainSocketPeerCredentials>,                          \
              unixSocketPeerCredentials, (), (const, override));                                   \
  MOCK_METHOD(void, setConnectionStats, (const ConnectionStats& stats), (override));               \
  MOCK_METHOD(Ssl::ConnectionInfoConstSharedPtr, ssl, (), (const, override));                      \
  MOCK_METHOD(absl::string_view, requestedServerName, (), (const, override));                      \
  MOCK_METHOD(absl::string_view, ja3Hash, (), (const));                                            \
  MOCK_METHOD(absl::string_view, ja4Hash, (), (const));                                            \
  MOCK_METHOD(State, state, (), (const, override));                                                \
  MOCK_METHOD(bool, connecting, (), (const, override));                                            \
  MOCK_METHOD(void, write, (Buffer::Instance & data, bool end_stream), (override));                \
  MOCK_METHOD(void, setBufferLimits, (uint32_t limit), (override));                                \
  MOCK_METHOD(void, setBufferHighWatermarkTimeout, (std::chrono::milliseconds timeout),            \
              (override));                                                                         \
  MOCK_METHOD(uint32_t, bufferLimit, (), (const, override));                                       \
  MOCK_METHOD(bool, aboveHighWatermark, (), (const, override));                                    \
  MOCK_METHOD(const ConnectionSocketPtr&, getSocket, (), (const, override));                       \
  MOCK_METHOD(const Network::ConnectionSocket::OptionsSharedPtr&, socketOptions, (),               \
              (const, override));                                                                  \
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, (), (override));                                \
  MOCK_METHOD(const StreamInfo::StreamInfo&, streamInfo, (), (const, override));                   \
  MOCK_METHOD(void, setDelayedCloseTimeout, (std::chrono::milliseconds), (override));              \
  MOCK_METHOD(absl::string_view, transportFailureReason, (), (const, override));                   \
  MOCK_METHOD(absl::string_view, localCloseReason, (), (const, override));                         \
  MOCK_METHOD(bool, startSecureTransport, (), (override));                                         \
  MOCK_METHOD(std::optional<std::chrono::milliseconds>, lastRoundTripTime, (), (const, override)); \
  MOCK_METHOD(void, configureInitialCongestionWindow,                                              \
              (uint64_t bandwidth_bits_per_sec, std::chrono::microseconds rtt), (override));       \
  MOCK_METHOD(std::optional<uint64_t>, congestionWindowInBytes, (), (const, override));            \
  MOCK_METHOD(void, dumpState, (std::ostream&, int), (const, override));                           \
  MOCK_METHOD(bool, setSocketOption, (Network::SocketOptionName, absl::Span<uint8_t>),             \
              (override));                                                                         \
  MOCK_METHOD(OptRef<const StreamInfo::StreamInfo>, trackedStream, (), (const, override));

class MockConnection : public Connection, public MockConnectionBase {
public:
  MockConnection();
  ~MockConnection() override;

  DEFINE_MOCK_CONNECTION_MOCK_METHODS;
};

class MockServerConnection : public ServerConnection, public MockConnectionBase {
public:
  MockServerConnection();
  ~MockServerConnection() override;

  DEFINE_MOCK_CONNECTION_MOCK_METHODS;

  // Network::ServerConnection
  MOCK_METHOD(void, setTransportSocketConnectTimeout, (std::chrono::milliseconds, Stats::Counter&),
              (override));
};

/**
 * NOTE: MockClientConnection duplicated most of MockConnection due to the fact that NiceMock
 *       cannot be reliably used on base class methods.
 */
class MockClientConnection : public ClientConnection, public MockConnectionBase {
public:
  MockClientConnection();
  ~MockClientConnection() override;

  DEFINE_MOCK_CONNECTION_MOCK_METHODS;

  // Network::ClientConnection
  MOCK_METHOD(void, connect, (), (override));
};

/**
 * NOTE: MockFilterManagerConnection duplicated most of MockConnection due to the fact that
 *       NiceMock cannot be reliably used on base class methods.
 */
class MockFilterManagerConnection : public FilterManagerConnection, public MockConnectionBase {
public:
  MockFilterManagerConnection();
  ~MockFilterManagerConnection() override;

  DEFINE_MOCK_CONNECTION_MOCK_METHODS;

  // Network::FilterManagerConnection
  MOCK_METHOD(StreamBuffer, getReadBuffer, (), (override));
  MOCK_METHOD(StreamBuffer, getWriteBuffer, (), (override));
  MOCK_METHOD(void, rawWrite, (Buffer::Instance & data, bool end_stream), (override));
  MOCK_METHOD(void, closeConnection, (ConnectionCloseAction close_action), (override));
  MOCK_METHOD(void, onFilterAboveHighWatermark, (), (override));
  MOCK_METHOD(void, onFilterBelowLowWatermark, (), (override));
};

} // namespace Network
} // namespace Envoy
