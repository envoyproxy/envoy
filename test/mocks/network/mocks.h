#pragma once

#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"

#include "common/network/proxy_protocol.h"

#include "test/mocks/event/mocks.h"

namespace Network {

class MockConnectionCallbacks : public ConnectionCallbacks {
public:
  MockConnectionCallbacks();
  ~MockConnectionCallbacks();

  // Network::ConnectionCallbacks
  MOCK_METHOD1(onEvent, void(uint32_t events));
};

class MockConnectionBase {
public:
  void raiseEvents(uint32_t events);

  static uint64_t next_id_;

  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  std::list<Network::ConnectionCallbacks*> callbacks_;
  uint64_t id_{next_id_++};
  Address::InstancePtr remote_address_;
  bool read_enabled_{true};
  Connection::State state_{Connection::State::Open};
};

class MockConnection : public Connection, public MockConnectionBase {
public:
  MockConnection();
  ~MockConnection();

  // Network::Connection
  MOCK_METHOD1(addConnectionCallbacks, void(ConnectionCallbacks& cb));
  MOCK_METHOD1(addWriteFilter, void(WriteFilterPtr filter));
  MOCK_METHOD1(addFilter, void(FilterPtr filter));
  MOCK_METHOD1(addReadFilter, void(ReadFilterPtr filter));
  MOCK_METHOD1(close, void(ConnectionCloseType type));
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(id, uint64_t());
  MOCK_METHOD0(initializeReadFilters, bool());
  MOCK_METHOD0(nextProtocol, std::string());
  MOCK_METHOD1(noDelay, void(bool enable));
  MOCK_METHOD1(readDisable, void(bool disable));
  MOCK_METHOD0(readEnabled, bool());
  MOCK_METHOD0(remoteAddress, const Address::Instance&());
  MOCK_METHOD0(localAddress, const Address::Instance&());
  MOCK_METHOD1(setBufferStats, void(const BufferStats& stats));
  MOCK_METHOD0(ssl, Ssl::Connection*());
  MOCK_METHOD0(state, State());
  MOCK_METHOD1(write, void(Buffer::Instance& data));
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
  MOCK_METHOD1(addWriteFilter, void(WriteFilterPtr filter));
  MOCK_METHOD1(addFilter, void(FilterPtr filter));
  MOCK_METHOD1(addReadFilter, void(ReadFilterPtr filter));
  MOCK_METHOD1(close, void(ConnectionCloseType type));
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(id, uint64_t());
  MOCK_METHOD0(initializeReadFilters, bool());
  MOCK_METHOD0(nextProtocol, std::string());
  MOCK_METHOD1(noDelay, void(bool enable));
  MOCK_METHOD1(readDisable, void(bool disable));
  MOCK_METHOD0(readEnabled, bool());
  MOCK_METHOD0(remoteAddress, const Address::Instance&());
  MOCK_METHOD0(localAddress, const Address::Instance&());
  MOCK_METHOD1(setBufferStats, void(const BufferStats& stats));
  MOCK_METHOD0(ssl, Ssl::Connection*());
  MOCK_METHOD0(state, State());
  MOCK_METHOD1(write, void(Buffer::Instance& data));

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
  MOCK_METHOD2(resolve, ActiveDnsQuery*(const std::string& dns_name, ResolveCb callback));

  testing::NiceMock<MockActiveDnsQuery> active_query_;
};

class MockReadFilterCallbacks : public ReadFilterCallbacks {
public:
  MockReadFilterCallbacks();
  ~MockReadFilterCallbacks();

  MOCK_METHOD0(connection, Connection&());
  MOCK_METHOD0(continueReading, void());
  MOCK_METHOD0(upstreamHost, Upstream::HostDescriptionPtr());
  MOCK_METHOD1(upstreamHost, void(Upstream::HostDescriptionPtr host));

  testing::NiceMock<MockConnection> connection_;
  Upstream::HostDescriptionPtr host_;
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

  MOCK_METHOD0(drainClose, bool());
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

  MOCK_METHOD0(localAddress, Address::InstancePtr());
  MOCK_METHOD0(fd, int());
  MOCK_METHOD0(close, void());

  Address::InstancePtr local_address_;
};

class MockListener : public Listener {
public:
  MockListener();
  ~MockListener();
};

class MockConnectionHandler : public ConnectionHandler {
public:
  MockConnectionHandler();
  ~MockConnectionHandler();

  MOCK_METHOD0(numConnections, uint64_t());
  MOCK_METHOD5(addListener,
               void(Network::FilterChainFactory& factory, Network::ListenSocket& socket,
                    bool bind_to_port, bool use_proxy_proto, bool use_orig_dst));
  MOCK_METHOD6(addSslListener, void(Network::FilterChainFactory& factory,
                                    Ssl::ServerContext& ssl_ctx, Network::ListenSocket& socket,
                                    bool bind_to_port, bool use_proxy_proto, bool use_orig_dst));
  MOCK_METHOD1(findListenerByPort, Network::Listener*(uint32_t port));
  MOCK_METHOD0(closeListeners, void());
};

} // Network
