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
  MOCK_METHOD1(onEvent, void(Network::ConnectionEvent event));
  MOCK_METHOD0(onAboveWriteBufferHighWatermark, void());
  MOCK_METHOD0(onBelowWriteBufferLowWatermark, void());
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

class MockConnection : public Connection, public MockConnectionBase {
public:
  MockConnection();
  ~MockConnection() override;

  // Network::Connection
  MOCK_METHOD1(addConnectionCallbacks, void(ConnectionCallbacks& cb));
  MOCK_METHOD1(addBytesSentCallback, void(BytesSentCb cb));
  MOCK_METHOD1(addWriteFilter, void(WriteFilterSharedPtr filter));
  MOCK_METHOD1(addFilter, void(FilterSharedPtr filter));
  MOCK_METHOD1(addReadFilter, void(ReadFilterSharedPtr filter));
  MOCK_METHOD1(enableHalfClose, void(bool enabled));
  MOCK_METHOD1(close, void(ConnectionCloseType type));
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_CONST_METHOD0(id, uint64_t());
  MOCK_METHOD0(initializeReadFilters, bool());
  MOCK_CONST_METHOD0(nextProtocol, std::string());
  MOCK_METHOD1(noDelay, void(bool enable));
  MOCK_METHOD1(readDisable, void(bool disable));
  MOCK_METHOD1(detectEarlyCloseWhenReadDisabled, void(bool));
  MOCK_CONST_METHOD0(readEnabled, bool());
  MOCK_CONST_METHOD0(remoteAddress, const Address::InstanceConstSharedPtr&());
  MOCK_CONST_METHOD0(unixSocketPeerCredentials,
                     absl::optional<Connection::UnixDomainSocketPeerCredentials>());
  MOCK_CONST_METHOD0(localAddress, const Address::InstanceConstSharedPtr&());
  MOCK_METHOD1(setConnectionStats, void(const ConnectionStats& stats));
  MOCK_CONST_METHOD0(ssl, Ssl::ConnectionInfoConstSharedPtr());
  MOCK_CONST_METHOD0(requestedServerName, absl::string_view());
  MOCK_CONST_METHOD0(state, State());
  MOCK_METHOD2(write, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(setBufferLimits, void(uint32_t limit));
  MOCK_CONST_METHOD0(bufferLimit, uint32_t());
  MOCK_CONST_METHOD0(localAddressRestored, bool());
  MOCK_CONST_METHOD0(aboveHighWatermark, bool());
  MOCK_CONST_METHOD0(socketOptions, const Network::ConnectionSocket::OptionsSharedPtr&());
  MOCK_METHOD0(streamInfo, StreamInfo::StreamInfo&());
  MOCK_CONST_METHOD0(streamInfo, const StreamInfo::StreamInfo&());
  MOCK_METHOD1(setDelayedCloseTimeout, void(std::chrono::milliseconds));
  MOCK_CONST_METHOD0(delayedCloseTimeout, std::chrono::milliseconds());
  MOCK_CONST_METHOD0(transportFailureReason, absl::string_view());
};

/**
 * NOTE: MockClientConnection duplicated most of MockConnection due to the fact that NiceMock
 *       cannot be reliably used on base class methods.
 */
class MockClientConnection : public ClientConnection, public MockConnectionBase {
public:
  MockClientConnection();
  ~MockClientConnection() override;

  // Network::Connection
  MOCK_METHOD1(addConnectionCallbacks, void(ConnectionCallbacks& cb));
  MOCK_METHOD1(addBytesSentCallback, void(BytesSentCb cb));
  MOCK_METHOD1(addWriteFilter, void(WriteFilterSharedPtr filter));
  MOCK_METHOD1(addFilter, void(FilterSharedPtr filter));
  MOCK_METHOD1(addReadFilter, void(ReadFilterSharedPtr filter));
  MOCK_METHOD1(enableHalfClose, void(bool enabled));
  MOCK_METHOD1(close, void(ConnectionCloseType type));
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_CONST_METHOD0(id, uint64_t());
  MOCK_METHOD0(initializeReadFilters, bool());
  MOCK_CONST_METHOD0(nextProtocol, std::string());
  MOCK_METHOD1(noDelay, void(bool enable));
  MOCK_METHOD1(readDisable, void(bool disable));
  MOCK_METHOD1(detectEarlyCloseWhenReadDisabled, void(bool));
  MOCK_CONST_METHOD0(readEnabled, bool());
  MOCK_CONST_METHOD0(remoteAddress, const Address::InstanceConstSharedPtr&());
  MOCK_CONST_METHOD0(unixSocketPeerCredentials,
                     absl::optional<Connection::UnixDomainSocketPeerCredentials>());
  MOCK_CONST_METHOD0(localAddress, const Address::InstanceConstSharedPtr&());
  MOCK_METHOD1(setConnectionStats, void(const ConnectionStats& stats));
  MOCK_CONST_METHOD0(ssl, Ssl::ConnectionInfoConstSharedPtr());
  MOCK_CONST_METHOD0(requestedServerName, absl::string_view());
  MOCK_CONST_METHOD0(state, State());
  MOCK_METHOD2(write, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(setBufferLimits, void(uint32_t limit));
  MOCK_CONST_METHOD0(bufferLimit, uint32_t());
  MOCK_CONST_METHOD0(localAddressRestored, bool());
  MOCK_CONST_METHOD0(aboveHighWatermark, bool());
  MOCK_CONST_METHOD0(socketOptions, const Network::ConnectionSocket::OptionsSharedPtr&());
  MOCK_METHOD0(streamInfo, StreamInfo::StreamInfo&());
  MOCK_CONST_METHOD0(streamInfo, const StreamInfo::StreamInfo&());
  MOCK_METHOD1(setDelayedCloseTimeout, void(std::chrono::milliseconds));
  MOCK_CONST_METHOD0(delayedCloseTimeout, std::chrono::milliseconds());
  MOCK_CONST_METHOD0(transportFailureReason, absl::string_view());

  // Network::ClientConnection
  MOCK_METHOD0(connect, void());
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
  MOCK_METHOD1(addConnectionCallbacks, void(ConnectionCallbacks& cb));
  MOCK_METHOD1(addBytesSentCallback, void(BytesSentCb cb));
  MOCK_METHOD1(addWriteFilter, void(WriteFilterSharedPtr filter));
  MOCK_METHOD1(addFilter, void(FilterSharedPtr filter));
  MOCK_METHOD1(addReadFilter, void(ReadFilterSharedPtr filter));
  MOCK_METHOD1(enableHalfClose, void(bool enabled));
  MOCK_METHOD1(close, void(ConnectionCloseType type));
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_CONST_METHOD0(id, uint64_t());
  MOCK_METHOD0(initializeReadFilters, bool());
  MOCK_CONST_METHOD0(nextProtocol, std::string());
  MOCK_METHOD1(noDelay, void(bool enable));
  MOCK_METHOD1(readDisable, void(bool disable));
  MOCK_METHOD1(detectEarlyCloseWhenReadDisabled, void(bool));
  MOCK_CONST_METHOD0(readEnabled, bool());
  MOCK_CONST_METHOD0(remoteAddress, const Address::InstanceConstSharedPtr&());
  MOCK_CONST_METHOD0(unixSocketPeerCredentials,
                     absl::optional<Connection::UnixDomainSocketPeerCredentials>());
  MOCK_CONST_METHOD0(localAddress, const Address::InstanceConstSharedPtr&());
  MOCK_METHOD1(setConnectionStats, void(const ConnectionStats& stats));
  MOCK_CONST_METHOD0(ssl, Ssl::ConnectionInfoConstSharedPtr());
  MOCK_CONST_METHOD0(requestedServerName, absl::string_view());
  MOCK_CONST_METHOD0(state, State());
  MOCK_METHOD2(write, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(setBufferLimits, void(uint32_t limit));
  MOCK_CONST_METHOD0(bufferLimit, uint32_t());
  MOCK_CONST_METHOD0(localAddressRestored, bool());
  MOCK_CONST_METHOD0(aboveHighWatermark, bool());
  MOCK_CONST_METHOD0(socketOptions, const Network::ConnectionSocket::OptionsSharedPtr&());
  MOCK_METHOD0(streamInfo, StreamInfo::StreamInfo&());
  MOCK_CONST_METHOD0(streamInfo, const StreamInfo::StreamInfo&());
  MOCK_METHOD1(setDelayedCloseTimeout, void(std::chrono::milliseconds));
  MOCK_CONST_METHOD0(delayedCloseTimeout, std::chrono::milliseconds());
  MOCK_CONST_METHOD0(transportFailureReason, absl::string_view());

  // Network::FilterManagerConnection
  MOCK_METHOD0(getReadBuffer, StreamBuffer());
  MOCK_METHOD0(getWriteBuffer, StreamBuffer());
  MOCK_METHOD2(rawWrite, void(Buffer::Instance& data, bool end_stream));
};

} // namespace Network
} // namespace Envoy
