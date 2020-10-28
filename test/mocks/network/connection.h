#pragma once

#include <list>

#include "envoy/network/connection.h"

#include "common/network/filter_manager_impl.h"

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
  MOCK_METHOD(void, onEvent, (Network::ConnectionEvent event));
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, ());
};

class MockConnectionBase {
public:
  void raiseEvent(Network::ConnectionEvent event);
  void raiseBytesSentCallbacks(uint64_t num_bytes);
  void runHighWatermarkCallbacks();
  void runLowWatermarkCallbacks();

  static uint64_t next_id_;

  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  std::list<Network::ConnectionCallbacks*> callbacks_;
  std::list<Network::Connection::BytesSentCb> bytes_sent_callbacks_;
  uint64_t id_{next_id_++};
  Address::InstanceConstSharedPtr remote_address_;
  Address::InstanceConstSharedPtr local_address_;
  bool read_enabled_{true};
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Connection::State state_{Connection::State::Open};
};

#define DEFINE_MOCK_CONNECTION_MOCK_METHODS                                                        \
  /* Network::Connection */                                                                        \
  MOCK_METHOD(void, addConnectionCallbacks, (ConnectionCallbacks & cb));                           \
  MOCK_METHOD(void, addBytesSentCallback, (BytesSentCb cb));                                       \
  MOCK_METHOD(void, addWriteFilter, (WriteFilterSharedPtr filter));                                \
  MOCK_METHOD(void, addFilter, (FilterSharedPtr filter));                                          \
  MOCK_METHOD(void, addReadFilter, (ReadFilterSharedPtr filter));                                  \
  MOCK_METHOD(void, enableHalfClose, (bool enabled));                                              \
  MOCK_METHOD(void, close, (ConnectionCloseType type));                                            \
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());                                                 \
  MOCK_METHOD(uint64_t, id, (), (const));                                                          \
  MOCK_METHOD(void, hashKey, (std::vector<uint8_t>&), (const));                                    \
  MOCK_METHOD(bool, initializeReadFilters, ());                                                    \
  MOCK_METHOD(std::string, nextProtocol, (), (const));                                             \
  MOCK_METHOD(void, noDelay, (bool enable));                                                       \
  MOCK_METHOD(void, readDisable, (bool disable));                                                  \
  MOCK_METHOD(void, detectEarlyCloseWhenReadDisabled, (bool));                                     \
  MOCK_METHOD(bool, readEnabled, (), (const));                                                     \
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, remoteAddress, (), (const));                 \
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, directRemoteAddress, (), (const));           \
  MOCK_METHOD(absl::optional<Connection::UnixDomainSocketPeerCredentials>,                         \
              unixSocketPeerCredentials, (), (const));                                             \
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, localAddress, (), (const));                  \
  MOCK_METHOD(void, setConnectionStats, (const ConnectionStats& stats));                           \
  MOCK_METHOD(Ssl::ConnectionInfoConstSharedPtr, ssl, (), (const));                                \
  MOCK_METHOD(absl::string_view, requestedServerName, (), (const));                                \
  MOCK_METHOD(State, state, (), (const));                                                          \
  MOCK_METHOD(void, write, (Buffer::Instance & data, bool end_stream));                            \
  MOCK_METHOD(void, setBufferLimits, (uint32_t limit));                                            \
  MOCK_METHOD(uint32_t, bufferLimit, (), (const));                                                 \
  MOCK_METHOD(bool, localAddressRestored, (), (const));                                            \
  MOCK_METHOD(bool, aboveHighWatermark, (), (const));                                              \
  MOCK_METHOD(const Network::ConnectionSocket::OptionsSharedPtr&, socketOptions, (), (const));     \
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());                                            \
  MOCK_METHOD(const StreamInfo::StreamInfo&, streamInfo, (), (const));                             \
  MOCK_METHOD(void, setDelayedCloseTimeout, (std::chrono::milliseconds));                          \
  MOCK_METHOD(absl::string_view, transportFailureReason, (), (const));                             \
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, lastRoundTripTime, (), (const))

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
  MOCK_METHOD(void, setTransportSocketConnectTimeout, (std::chrono::milliseconds));
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
  MOCK_METHOD(void, connect, ());
};

/**
 * NOTE: MockFilterManagerConnection duplicated most of MockConnection due to the fact that
 *       NiceMock cannot be reliably used on base class methods.
 */
class MockFilterManagerConnection : public FilterManagerConnection, public MockConnectionBase {
public:
  MockFilterManagerConnection();
  ~MockFilterManagerConnection() override;

  // Network::Connection
  MOCK_METHOD(void, addConnectionCallbacks, (ConnectionCallbacks & cb));
  MOCK_METHOD(void, addBytesSentCallback, (BytesSentCb cb));
  MOCK_METHOD(void, addWriteFilter, (WriteFilterSharedPtr filter));
  MOCK_METHOD(void, addFilter, (FilterSharedPtr filter));
  MOCK_METHOD(void, addReadFilter, (ReadFilterSharedPtr filter));
  MOCK_METHOD(void, enableHalfClose, (bool enabled));
  MOCK_METHOD(void, close, (ConnectionCloseType type));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(uint64_t, id, (), (const));
  MOCK_METHOD(void, hashKey, (std::vector<uint8_t>&), (const));
  MOCK_METHOD(bool, initializeReadFilters, ());
  MOCK_METHOD(std::string, nextProtocol, (), (const));
  MOCK_METHOD(void, noDelay, (bool enable));
  MOCK_METHOD(void, readDisable, (bool disable));
  MOCK_METHOD(void, detectEarlyCloseWhenReadDisabled, (bool));
  MOCK_METHOD(bool, readEnabled, (), (const));
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, remoteAddress, (), (const));
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, directRemoteAddress, (), (const));
  MOCK_METHOD(absl::optional<Connection::UnixDomainSocketPeerCredentials>,
              unixSocketPeerCredentials, (), (const));
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, localAddress, (), (const));
  MOCK_METHOD(void, setConnectionStats, (const ConnectionStats& stats));
  MOCK_METHOD(Ssl::ConnectionInfoConstSharedPtr, ssl, (), (const));
  MOCK_METHOD(absl::string_view, requestedServerName, (), (const));
  MOCK_METHOD(State, state, (), (const));
  MOCK_METHOD(void, write, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, setBufferLimits, (uint32_t limit));
  MOCK_METHOD(uint32_t, bufferLimit, (), (const));
  MOCK_METHOD(bool, localAddressRestored, (), (const));
  MOCK_METHOD(bool, aboveHighWatermark, (), (const));
  MOCK_METHOD(const Network::ConnectionSocket::OptionsSharedPtr&, socketOptions, (), (const));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(const StreamInfo::StreamInfo&, streamInfo, (), (const));
  MOCK_METHOD(void, setDelayedCloseTimeout, (std::chrono::milliseconds));
  MOCK_METHOD(absl::string_view, transportFailureReason, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, lastRoundTripTime, (), (const));

  // Network::FilterManagerConnection
  MOCK_METHOD(StreamBuffer, getReadBuffer, ());
  MOCK_METHOD(StreamBuffer, getWriteBuffer, ());
  MOCK_METHOD(void, rawWrite, (Buffer::Instance & data, bool end_stream));
};

} // namespace Network
} // namespace Envoy
