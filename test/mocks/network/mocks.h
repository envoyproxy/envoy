#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "envoy/api/v2/core/address.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/network/resolver.h"
#include "envoy/network/transport_socket.h"

#include "common/stats/stats_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Network {

class MockConnectionCallbacks : public ConnectionCallbacks {
public:
  MockConnectionCallbacks();
  ~MockConnectionCallbacks();

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
  Connection::State state_{Connection::State::Open};
};

class MockConnection : public Connection, public MockConnectionBase {
public:
  MockConnection();
  ~MockConnection();

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
  MOCK_CONST_METHOD0(localAddress, const Address::InstanceConstSharedPtr&());
  MOCK_METHOD1(setConnectionStats, void(const ConnectionStats& stats));
  MOCK_METHOD0(ssl, Ssl::Connection*());
  MOCK_CONST_METHOD0(ssl, const Ssl::Connection*());
  MOCK_CONST_METHOD0(state, State());
  MOCK_METHOD2(write, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(setBufferLimits, void(uint32_t limit));
  MOCK_CONST_METHOD0(bufferLimit, uint32_t());
  MOCK_CONST_METHOD0(localAddressRestored, bool());
  MOCK_CONST_METHOD0(aboveHighWatermark, bool());
  MOCK_CONST_METHOD0(socketOptions, const Network::ConnectionSocket::OptionsSharedPtr&());
};

/**
 * NOTE: MockClientConnection duplicated most of MockConnection due to the fact that NiceMock
 *       cannot be reliably used on base class methods.
 */
class MockClientConnection : public ClientConnection, public MockConnectionBase {
public:
  MockClientConnection();
  ~MockClientConnection();

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
  MOCK_CONST_METHOD0(localAddress, const Address::InstanceConstSharedPtr&());
  MOCK_METHOD1(setConnectionStats, void(const ConnectionStats& stats));
  MOCK_METHOD0(ssl, Ssl::Connection*());
  MOCK_CONST_METHOD0(ssl, const Ssl::Connection*());
  MOCK_CONST_METHOD0(state, State());
  MOCK_METHOD2(write, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(setBufferLimits, void(uint32_t limit));
  MOCK_CONST_METHOD0(bufferLimit, uint32_t());
  MOCK_CONST_METHOD0(localAddressRestored, bool());
  MOCK_CONST_METHOD0(aboveHighWatermark, bool());
  MOCK_CONST_METHOD0(socketOptions, const Network::ConnectionSocket::OptionsSharedPtr&());

  // Network::ClientConnection
  MOCK_METHOD0(connect, void());
};

class MockActiveDnsQuery : public ActiveDnsQuery {
public:
  MockActiveDnsQuery();
  ~MockActiveDnsQuery();

  // Network::ActiveDnsQuery
  MOCK_METHOD0(cancel, void());
};

class MockDnsResolver : public DnsResolver {
public:
  MockDnsResolver();
  ~MockDnsResolver();

  // Network::DnsResolver
  MOCK_METHOD3(resolve, ActiveDnsQuery*(const std::string& dns_name,
                                        DnsLookupFamily dns_lookup_family, ResolveCb callback));

  testing::NiceMock<MockActiveDnsQuery> active_query_;
};

class MockAddressResolver : public Address::Resolver {
public:
  MockAddressResolver();
  ~MockAddressResolver();

  MOCK_METHOD1(resolve,
               Address::InstanceConstSharedPtr(const envoy::api::v2::core::SocketAddress&));
  MOCK_CONST_METHOD0(name, std::string());
};

class MockReadFilterCallbacks : public ReadFilterCallbacks {
public:
  MockReadFilterCallbacks();
  ~MockReadFilterCallbacks();

  MOCK_METHOD0(connection, Connection&());
  MOCK_METHOD0(continueReading, void());
  MOCK_METHOD0(upstreamHost, Upstream::HostDescriptionConstSharedPtr());
  MOCK_METHOD1(upstreamHost, void(Upstream::HostDescriptionConstSharedPtr host));

  testing::NiceMock<MockConnection> connection_;
  Upstream::HostDescriptionConstSharedPtr host_;
};

class MockReadFilter : public ReadFilter {
public:
  MockReadFilter();
  ~MockReadFilter();

  MOCK_METHOD2(onData, FilterStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD0(onNewConnection, FilterStatus());
  MOCK_METHOD1(initializeReadFilterCallbacks, void(ReadFilterCallbacks& callbacks));

  ReadFilterCallbacks* callbacks_{};
};

class MockWriteFilter : public WriteFilter {
public:
  MockWriteFilter();
  ~MockWriteFilter();

  MOCK_METHOD2(onWrite, FilterStatus(Buffer::Instance& data, bool end_stream));
};

class MockFilter : public Filter {
public:
  MockFilter();
  ~MockFilter();

  MOCK_METHOD2(onData, FilterStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD0(onNewConnection, FilterStatus());
  MOCK_METHOD2(onWrite, FilterStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(initializeReadFilterCallbacks, void(ReadFilterCallbacks& callbacks));

  ReadFilterCallbacks* callbacks_{};
};

class MockListenerCallbacks : public ListenerCallbacks {
public:
  MockListenerCallbacks();
  ~MockListenerCallbacks();

  void onAccept(ConnectionSocketPtr&& socket, bool redirected) override {
    onAccept_(socket, redirected);
  }
  void onNewConnection(ConnectionPtr&& conn) override { onNewConnection_(conn); }

  MOCK_METHOD2(onAccept_, void(ConnectionSocketPtr& socket, bool redirected));
  MOCK_METHOD1(onNewConnection_, void(ConnectionPtr& conn));
};

class MockDrainDecision : public DrainDecision {
public:
  MockDrainDecision();
  ~MockDrainDecision();

  MOCK_CONST_METHOD0(drainClose, bool());
};

class MockListenerFilter : public Network::ListenerFilter {
public:
  MockListenerFilter();
  ~MockListenerFilter();

  MOCK_METHOD1(onAccept, Network::FilterStatus(Network::ListenerFilterCallbacks&));
};

class MockListenerFilterCallbacks : public ListenerFilterCallbacks {
public:
  MockListenerFilterCallbacks();
  ~MockListenerFilterCallbacks();

  MOCK_METHOD0(socket, ConnectionSocket&());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD1(continueFilterChain, void(bool));
};

class MockListenerFilterManager : public ListenerFilterManager {
public:
  MockListenerFilterManager();
  ~MockListenerFilterManager();

  void addAcceptFilter(Network::ListenerFilterPtr&& filter) override { addAcceptFilter_(filter); }

  MOCK_METHOD1(addAcceptFilter_, void(Network::ListenerFilterPtr&));
};

class MockFilterChainFactory : public FilterChainFactory {
public:
  MockFilterChainFactory();
  ~MockFilterChainFactory();

  MOCK_METHOD1(createNetworkFilterChain, bool(Connection& connection));
  MOCK_METHOD1(createListenerFilterChain, bool(ListenerFilterManager& listener));
};

class MockListenSocket : public Socket {
public:
  MockListenSocket();
  ~MockListenSocket();

  void addOption(const Socket::OptionConstSharedPtr& option) override { addOption_(option); }
  void addOptions(const Socket::OptionsSharedPtr& options) override { addOptions_(options); }

  MOCK_CONST_METHOD0(localAddress, const Address::InstanceConstSharedPtr&());
  MOCK_CONST_METHOD0(fd, int());
  MOCK_METHOD0(close, void());
  MOCK_METHOD1(addOption_, void(const Socket::OptionConstSharedPtr& option));
  MOCK_METHOD1(addOptions_, void(const Socket::OptionsSharedPtr& options));
  MOCK_CONST_METHOD0(options, const OptionsSharedPtr&());

  Address::InstanceConstSharedPtr local_address_;
  OptionsSharedPtr options_;
};

class MockSocketOption : public Socket::Option {
public:
  MockSocketOption();
  ~MockSocketOption();

  MOCK_CONST_METHOD2(setOption, bool(Socket&, Network::Socket::SocketState state));
  MOCK_CONST_METHOD1(hashKey, void(std::vector<uint8_t>&));
};

class MockConnectionSocket : public ConnectionSocket {
public:
  MockConnectionSocket();
  ~MockConnectionSocket();

  void addOption(const Socket::OptionConstSharedPtr& option) override { addOption_(option); }
  void addOptions(const Socket::OptionsSharedPtr& options) override { addOptions_(options); }

  MOCK_CONST_METHOD0(localAddress, const Address::InstanceConstSharedPtr&());
  MOCK_METHOD2(setLocalAddress, void(const Address::InstanceConstSharedPtr&, bool));
  MOCK_CONST_METHOD0(localAddressRestored, bool());
  MOCK_METHOD1(setRemoteAddress, void(const Address::InstanceConstSharedPtr&));
  MOCK_CONST_METHOD0(remoteAddress, const Address::InstanceConstSharedPtr&());
  MOCK_METHOD1(setDetectedTransportProtocol, void(absl::string_view));
  MOCK_CONST_METHOD0(detectedTransportProtocol, absl::string_view());
  MOCK_METHOD1(setRequestedApplicationProtocols, void(const std::vector<absl::string_view>&));
  MOCK_CONST_METHOD0(requestedApplicationProtocols, const std::vector<std::string>&());
  MOCK_METHOD1(setRequestedServerName, void(absl::string_view));
  MOCK_CONST_METHOD0(requestedServerName, absl::string_view());
  MOCK_METHOD1(addOption_, void(const Socket::OptionConstSharedPtr&));
  MOCK_METHOD1(addOptions_, void(const Socket::OptionsSharedPtr&));
  MOCK_CONST_METHOD0(options, const Network::ConnectionSocket::OptionsSharedPtr&());
  MOCK_CONST_METHOD0(fd, int());
  MOCK_METHOD0(close, void());

  Address::InstanceConstSharedPtr local_address_;
};

class MockListenerConfig : public ListenerConfig {
public:
  MockListenerConfig();
  ~MockListenerConfig();

  MOCK_METHOD0(filterChainFactory, FilterChainFactory&());
  MOCK_METHOD0(socket, Socket&());
  MOCK_METHOD0(transportSocketFactory, TransportSocketFactory&());
  MOCK_METHOD0(bindToPort, bool());
  MOCK_CONST_METHOD0(handOffRestoredDestinationConnections, bool());
  MOCK_METHOD0(perConnectionBufferLimitBytes, uint32_t());
  MOCK_METHOD0(listenerScope, Stats::Scope&());
  MOCK_CONST_METHOD0(listenerTag, uint64_t());
  MOCK_CONST_METHOD0(name, const std::string&());

  testing::NiceMock<MockFilterChainFactory> filter_chain_factory_;
  testing::NiceMock<MockListenSocket> socket_;
  Stats::IsolatedStoreImpl scope_;
  std::string name_;
};

class MockListener : public Listener {
public:
  MockListener();
  ~MockListener();

  MOCK_METHOD0(onDestroy, void());
};

class MockConnectionHandler : public ConnectionHandler {
public:
  MockConnectionHandler();
  ~MockConnectionHandler();

  MOCK_METHOD0(numConnections, uint64_t());
  MOCK_METHOD1(addListener, void(ListenerConfig& config));
  MOCK_METHOD1(findListenerByAddress,
               Network::Listener*(const Network::Address::Instance& address));
  MOCK_METHOD1(removeListeners, void(uint64_t listener_tag));
  MOCK_METHOD1(stopListeners, void(uint64_t listener_tag));
  MOCK_METHOD0(stopListeners, void());
};

class MockResolvedAddress : public Address::Instance {
public:
  MockResolvedAddress(const std::string& logical, const std::string& physical)
      : logical_(logical), physical_(physical) {}

  bool operator==(const Address::Instance& other) const override {
    return asString() == other.asString();
  }

  MOCK_CONST_METHOD1(bind, int(int));
  MOCK_CONST_METHOD1(connect, int(int));
  MOCK_CONST_METHOD0(ip, Address::Ip*());
  MOCK_CONST_METHOD1(socket, int(Address::SocketType));
  MOCK_CONST_METHOD0(type, Address::Type());

  const std::string& asString() const override { return physical_; }
  const std::string& logicalName() const override { return logical_; }

  const std::string logical_;
  const std::string physical_;
};

class MockTransportSocket : public TransportSocket {
public:
  MockTransportSocket();
  ~MockTransportSocket();

  MOCK_METHOD1(setTransportSocketCallbacks, void(TransportSocketCallbacks& callbacks));
  MOCK_CONST_METHOD0(protocol, std::string());
  MOCK_METHOD0(canFlushClose, bool());
  MOCK_METHOD1(closeSocket, void(Network::ConnectionEvent event));
  MOCK_METHOD1(doRead, IoResult(Buffer::Instance& buffer));
  MOCK_METHOD2(doWrite, IoResult(Buffer::Instance& buffer, bool end_stream));
  MOCK_METHOD0(onConnected, void());
  MOCK_METHOD0(ssl, Ssl::Connection*());
  MOCK_CONST_METHOD0(ssl, const Ssl::Connection*());
};

class MockTransportSocketFactory : public TransportSocketFactory {
public:
  MockTransportSocketFactory();
  ~MockTransportSocketFactory();

  MOCK_CONST_METHOD0(implementsSecureTransport, bool());
  MOCK_CONST_METHOD0(createTransportSocket, TransportSocketPtr());
};

} // namespace Network
} // namespace Envoy
