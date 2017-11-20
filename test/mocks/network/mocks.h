#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"

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
  void runHighWatermarkCallbacks();
  void runLowWatermarkCallbacks();

  static uint64_t next_id_;

  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  std::list<Network::ConnectionCallbacks*> callbacks_;
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
  MOCK_METHOD1(addWriteFilter, void(WriteFilterSharedPtr filter));
  MOCK_METHOD1(addFilter, void(FilterSharedPtr filter));
  MOCK_METHOD1(addReadFilter, void(ReadFilterSharedPtr filter));
  MOCK_METHOD1(close, void(ConnectionCloseType type));
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_CONST_METHOD0(id, uint64_t());
  MOCK_METHOD0(initializeReadFilters, bool());
  MOCK_CONST_METHOD0(nextProtocol, std::string());
  MOCK_METHOD1(noDelay, void(bool enable));
  MOCK_METHOD1(readDisable, void(bool disable));
  MOCK_METHOD1(detectEarlyCloseWhenReadDisabled, void(bool));
  MOCK_CONST_METHOD0(readEnabled, bool());
  MOCK_CONST_METHOD0(remoteAddress, const Address::Instance&());
  MOCK_CONST_METHOD0(localAddress, const Address::Instance&());
  MOCK_METHOD1(setConnectionStats, void(const ConnectionStats& stats));
  MOCK_METHOD0(ssl, Ssl::Connection*());
  MOCK_CONST_METHOD0(ssl, const Ssl::Connection*());
  MOCK_CONST_METHOD0(state, State());
  MOCK_METHOD1(write, void(Buffer::Instance& data));
  MOCK_METHOD1(setBufferLimits, void(uint32_t limit));
  MOCK_CONST_METHOD0(bufferLimit, uint32_t());
  MOCK_CONST_METHOD0(usingOriginalDst, bool());
  MOCK_CONST_METHOD0(aboveHighWatermark, bool());
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
  MOCK_METHOD1(addWriteFilter, void(WriteFilterSharedPtr filter));
  MOCK_METHOD1(addFilter, void(FilterSharedPtr filter));
  MOCK_METHOD1(addReadFilter, void(ReadFilterSharedPtr filter));
  MOCK_METHOD1(close, void(ConnectionCloseType type));
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_CONST_METHOD0(id, uint64_t());
  MOCK_METHOD0(initializeReadFilters, bool());
  MOCK_CONST_METHOD0(nextProtocol, std::string());
  MOCK_METHOD1(noDelay, void(bool enable));
  MOCK_METHOD1(readDisable, void(bool disable));
  MOCK_METHOD1(detectEarlyCloseWhenReadDisabled, void(bool));
  MOCK_CONST_METHOD0(readEnabled, bool());
  MOCK_CONST_METHOD0(remoteAddress, const Address::Instance&());
  MOCK_CONST_METHOD0(localAddress, const Address::Instance&());
  MOCK_METHOD1(setConnectionStats, void(const ConnectionStats& stats));
  MOCK_METHOD0(ssl, Ssl::Connection*());
  MOCK_CONST_METHOD0(ssl, const Ssl::Connection*());
  MOCK_CONST_METHOD0(state, State());
  MOCK_METHOD1(write, void(Buffer::Instance& data));
  MOCK_METHOD1(setBufferLimits, void(uint32_t limit));
  MOCK_CONST_METHOD0(bufferLimit, uint32_t());
  MOCK_CONST_METHOD0(usingOriginalDst, bool());
  MOCK_CONST_METHOD0(aboveHighWatermark, bool());

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

  MOCK_METHOD1(onData, FilterStatus(Buffer::Instance& data));
  MOCK_METHOD0(onNewConnection, FilterStatus());
  MOCK_METHOD1(initializeReadFilterCallbacks, void(ReadFilterCallbacks& callbacks));

  ReadFilterCallbacks* callbacks_{};
};

class MockWriteFilter : public WriteFilter {
public:
  MockWriteFilter();
  ~MockWriteFilter();

  MOCK_METHOD1(onWrite, FilterStatus(Buffer::Instance& data));
};

class MockFilter : public Filter {
public:
  MockFilter();
  ~MockFilter();

  MOCK_METHOD1(onData, FilterStatus(Buffer::Instance& data));
  MOCK_METHOD0(onNewConnection, FilterStatus());
  MOCK_METHOD1(onWrite, FilterStatus(Buffer::Instance& data));
  MOCK_METHOD1(initializeReadFilterCallbacks, void(ReadFilterCallbacks& callbacks));

  ReadFilterCallbacks* callbacks_{};
};

class MockListenerCallbacks : public ListenerCallbacks {
public:
  MockListenerCallbacks();
  ~MockListenerCallbacks();

  void onNewConnection(ConnectionPtr&& conn) override { onNewConnection_(conn); }

  MOCK_METHOD1(onNewConnection_, void(ConnectionPtr& conn));
};

class MockDrainDecision : public DrainDecision {
public:
  MockDrainDecision();
  ~MockDrainDecision();

  MOCK_CONST_METHOD0(drainClose, bool());
};

class MockFilterChainFactory : public FilterChainFactory {
public:
  MockFilterChainFactory();
  ~MockFilterChainFactory();

  MOCK_METHOD1(createFilterChain, bool(Connection& connection));
};

class MockListenSocket : public ListenSocket {
public:
  MockListenSocket();
  ~MockListenSocket();

  MOCK_CONST_METHOD0(localAddress, Address::InstanceConstSharedPtr());
  MOCK_METHOD0(fd, int());
  MOCK_METHOD0(close, void());

  Address::InstanceConstSharedPtr local_address_;
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
  MOCK_METHOD5(addListener,
               void(Network::FilterChainFactory& factory, Network::ListenSocket& socket,
                    Stats::Scope& scope, uint64_t listener_tag,
                    const Network::ListenerOptions& listener_options));
  MOCK_METHOD6(addSslListener,
               void(Network::FilterChainFactory& factory, Ssl::ServerContext& ssl_ctx,
                    Network::ListenSocket& socket, Stats::Scope& scope, uint64_t listener_tag,
                    const Network::ListenerOptions& listener_options));
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

} // namespace Network
} // namespace Envoy
