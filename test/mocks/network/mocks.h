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
#include "envoy/stats/scope.h"

#include "common/network/filter_manager_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Network {

class MockActiveDnsQuery : public ActiveDnsQuery {
public:
  MockActiveDnsQuery();
  ~MockActiveDnsQuery() override;

  // Network::ActiveDnsQuery
  MOCK_METHOD0(cancel, void());
};

class MockDnsResolver : public DnsResolver {
public:
  MockDnsResolver();
  ~MockDnsResolver() override;

  // Network::DnsResolver
  MOCK_METHOD3(resolve, ActiveDnsQuery*(const std::string& dns_name,
                                        DnsLookupFamily dns_lookup_family, ResolveCb callback));

  testing::NiceMock<MockActiveDnsQuery> active_query_;
};

class MockAddressResolver : public Address::Resolver {
public:
  MockAddressResolver();
  ~MockAddressResolver() override;

  MOCK_METHOD1(resolve,
               Address::InstanceConstSharedPtr(const envoy::api::v2::core::SocketAddress&));
  MOCK_CONST_METHOD0(name, std::string());
};

class MockReadFilterCallbacks : public ReadFilterCallbacks {
public:
  MockReadFilterCallbacks();
  ~MockReadFilterCallbacks() override;

  MOCK_METHOD0(connection, Connection&());
  MOCK_METHOD0(continueReading, void());
  MOCK_METHOD2(injectReadDataToFilterChain, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD0(upstreamHost, Upstream::HostDescriptionConstSharedPtr());
  MOCK_METHOD1(upstreamHost, void(Upstream::HostDescriptionConstSharedPtr host));

  testing::NiceMock<MockConnection> connection_;
  Upstream::HostDescriptionConstSharedPtr host_;
};

class MockReadFilter : public ReadFilter {
public:
  MockReadFilter();
  ~MockReadFilter() override;

  MOCK_METHOD2(onData, FilterStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD0(onNewConnection, FilterStatus());
  MOCK_METHOD1(initializeReadFilterCallbacks, void(ReadFilterCallbacks& callbacks));

  ReadFilterCallbacks* callbacks_{};
};

class MockWriteFilterCallbacks : public WriteFilterCallbacks {
public:
  MockWriteFilterCallbacks();
  ~MockWriteFilterCallbacks() override;

  MOCK_METHOD0(connection, Connection&());
  MOCK_METHOD2(injectWriteDataToFilterChain, void(Buffer::Instance& data, bool end_stream));

  testing::NiceMock<MockConnection> connection_;
};

class MockWriteFilter : public WriteFilter {
public:
  MockWriteFilter();
  ~MockWriteFilter() override;

  MOCK_METHOD2(onWrite, FilterStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(initializeWriteFilterCallbacks, void(WriteFilterCallbacks& callbacks));

  WriteFilterCallbacks* write_callbacks_{};
};

class MockFilter : public Filter {
public:
  MockFilter();
  ~MockFilter() override;

  MOCK_METHOD2(onData, FilterStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD0(onNewConnection, FilterStatus());
  MOCK_METHOD2(onWrite, FilterStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(initializeReadFilterCallbacks, void(ReadFilterCallbacks& callbacks));
  MOCK_METHOD1(initializeWriteFilterCallbacks, void(WriteFilterCallbacks& callbacks));

  ReadFilterCallbacks* callbacks_{};
  WriteFilterCallbacks* write_callbacks_{};
};

class MockListenerCallbacks : public ListenerCallbacks {
public:
  MockListenerCallbacks();
  ~MockListenerCallbacks() override;

  void onAccept(ConnectionSocketPtr&& socket) override { onAccept_(socket); }

  MOCK_METHOD1(onAccept_, void(ConnectionSocketPtr& socket));
};

class MockUdpListenerCallbacks : public UdpListenerCallbacks {
public:
  MockUdpListenerCallbacks();
  ~MockUdpListenerCallbacks() override;

  void onData(UdpRecvData& data) override { onData_(data); }

  void onWriteReady(const Socket& socket) override { onWriteReady_(socket); }

  void onReceiveError(const ErrorCode& err_code, Api::IoError::IoErrorCode err) override {
    onReceiveError_(err_code, err);
  }

  MOCK_METHOD1(onData_, void(UdpRecvData& data));

  MOCK_METHOD1(onWriteReady_, void(const Socket& socket));

  MOCK_METHOD2(onReceiveError_, void(const ErrorCode& err_code, Api::IoError::IoErrorCode err));
};

class MockDrainDecision : public DrainDecision {
public:
  MockDrainDecision();
  ~MockDrainDecision() override;

  MOCK_CONST_METHOD0(drainClose, bool());
};

class MockListenerFilter : public ListenerFilter {
public:
  MockListenerFilter();
  ~MockListenerFilter() override;

  MOCK_METHOD1(onAccept, Network::FilterStatus(ListenerFilterCallbacks&));
};

class MockListenerFilterManager : public ListenerFilterManager {
public:
  MockListenerFilterManager();
  ~MockListenerFilterManager() override;

  void addAcceptFilter(ListenerFilterPtr&& filter) override { addAcceptFilter_(filter); }

  MOCK_METHOD1(addAcceptFilter_, void(Network::ListenerFilterPtr&));
};

class MockFilterChain : public FilterChain {
public:
  MockFilterChain();
  ~MockFilterChain() override;

  // Network::FilterChain
  MOCK_CONST_METHOD0(transportSocketFactory, const TransportSocketFactory&());
  MOCK_CONST_METHOD0(networkFilterFactories, const std::vector<FilterFactoryCb>&());
};

class MockFilterChainManager : public FilterChainManager {
public:
  MockFilterChainManager();
  ~MockFilterChainManager() override;

  // Network::FilterChainManager
  MOCK_CONST_METHOD1(findFilterChain, const FilterChain*(const ConnectionSocket& socket));
};

class MockFilterChainFactory : public FilterChainFactory {
public:
  MockFilterChainFactory();
  ~MockFilterChainFactory() override;

  MOCK_METHOD2(createNetworkFilterChain,
               bool(Connection& connection,
                    const std::vector<Network::FilterFactoryCb>& filter_factories));
  MOCK_METHOD1(createListenerFilterChain, bool(ListenerFilterManager& listener));
  MOCK_METHOD2(createUdpListenerFilterChain,
               bool(UdpListenerFilterManager& listener, UdpReadFilterCallbacks& callbacks));
};

class MockListenSocket : public Socket {
public:
  MockListenSocket();
  ~MockListenSocket() override = default;

  void addOption(const Socket::OptionConstSharedPtr& option) override { addOption_(option); }
  void addOptions(const Socket::OptionsSharedPtr& options) override { addOptions_(options); }

  MOCK_CONST_METHOD0(localAddress, const Address::InstanceConstSharedPtr&());
  MOCK_METHOD1(setLocalAddress, void(const Address::InstanceConstSharedPtr&));
  MOCK_METHOD0(ioHandle, IoHandle&());
  MOCK_CONST_METHOD0(ioHandle, const IoHandle&());
  MOCK_CONST_METHOD0(socketType, Address::SocketType());
  MOCK_METHOD0(close, void());
  MOCK_CONST_METHOD0(isOpen, bool());
  MOCK_METHOD1(addOption_, void(const Socket::OptionConstSharedPtr& option));
  MOCK_METHOD1(addOptions_, void(const Socket::OptionsSharedPtr& options));
  MOCK_CONST_METHOD0(options, const OptionsSharedPtr&());

  IoHandlePtr io_handle_;
  Address::InstanceConstSharedPtr local_address_;
  OptionsSharedPtr options_;
  bool socket_is_open_ = true;
};

class MockSocketOption : public Socket::Option {
public:
  MockSocketOption();
  ~MockSocketOption() override;

  MOCK_CONST_METHOD2(setOption,
                     bool(Socket&, envoy::api::v2::core::SocketOption::SocketState state));
  MOCK_CONST_METHOD1(hashKey, void(std::vector<uint8_t>&));
  MOCK_CONST_METHOD2(getOptionDetails,
                     absl::optional<Socket::Option::Details>(
                         const Socket&, envoy::api::v2::core::SocketOption::SocketState state));
};

class MockConnectionSocket : public ConnectionSocket {
public:
  MockConnectionSocket();
  ~MockConnectionSocket() override;

  void addOption(const Socket::OptionConstSharedPtr& option) override { addOption_(option); }
  void addOptions(const Socket::OptionsSharedPtr& options) override { addOptions_(options); }

  MOCK_CONST_METHOD0(localAddress, const Address::InstanceConstSharedPtr&());
  MOCK_METHOD1(setLocalAddress, void(const Address::InstanceConstSharedPtr&));
  MOCK_METHOD1(restoreLocalAddress, void(const Address::InstanceConstSharedPtr&));
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
  MOCK_METHOD0(ioHandle, IoHandle&());
  MOCK_CONST_METHOD0(ioHandle, const IoHandle&());
  MOCK_CONST_METHOD0(socketType, Address::SocketType());
  MOCK_METHOD0(close, void());
  MOCK_CONST_METHOD0(isOpen, bool());

  IoHandlePtr io_handle_;
  Address::InstanceConstSharedPtr local_address_;
  Address::InstanceConstSharedPtr remote_address_;
  bool is_closed_;
};

class MockListenerFilterCallbacks : public ListenerFilterCallbacks {
public:
  MockListenerFilterCallbacks();
  ~MockListenerFilterCallbacks() override;

  MOCK_METHOD0(socket, ConnectionSocket&());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD1(continueFilterChain, void(bool));

  NiceMock<MockConnectionSocket> socket_;
};

class MockListenerConfig : public ListenerConfig {
public:
  MockListenerConfig();
  ~MockListenerConfig() override;

  MOCK_METHOD0(filterChainManager, FilterChainManager&());
  MOCK_METHOD0(filterChainFactory, FilterChainFactory&());
  MOCK_METHOD0(socket, Socket&());
  MOCK_CONST_METHOD0(socket, const Socket&());
  MOCK_METHOD0(bindToPort, bool());
  MOCK_CONST_METHOD0(handOffRestoredDestinationConnections, bool());
  MOCK_CONST_METHOD0(perConnectionBufferLimitBytes, uint32_t());
  MOCK_CONST_METHOD0(listenerFiltersTimeout, std::chrono::milliseconds());
  MOCK_CONST_METHOD0(continueOnListenerFiltersTimeout, bool());
  MOCK_METHOD0(listenerScope, Stats::Scope&());
  MOCK_CONST_METHOD0(listenerTag, uint64_t());
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_METHOD0(udpListenerFactory, const Network::ActiveUdpListenerFactory*());
  MOCK_METHOD0(connectionBalancer, ConnectionBalancer&());

  envoy::api::v2::core::TrafficDirection direction() const override {
    return envoy::api::v2::core::TrafficDirection::UNSPECIFIED;
  }

  testing::NiceMock<MockFilterChainFactory> filter_chain_factory_;
  testing::NiceMock<MockListenSocket> socket_;
  Stats::IsolatedStoreImpl scope_;
  std::string name_;
};

class MockListener : public Listener {
public:
  MockListener();
  ~MockListener() override;

  MOCK_METHOD0(onDestroy, void());
  MOCK_METHOD0(enable, void());
  MOCK_METHOD0(disable, void());
};

class MockConnectionHandler : public ConnectionHandler {
public:
  MockConnectionHandler();
  ~MockConnectionHandler() override;

  MOCK_METHOD0(numConnections, uint64_t());
  MOCK_METHOD0(incNumConnections, void());
  MOCK_METHOD0(decNumConnections, void());
  MOCK_METHOD1(addListener, void(ListenerConfig& config));
  MOCK_METHOD1(removeListeners, void(uint64_t listener_tag));
  MOCK_METHOD1(stopListeners, void(uint64_t listener_tag));
  MOCK_METHOD0(stopListeners, void());
  MOCK_METHOD0(disableListeners, void());
  MOCK_METHOD0(enableListeners, void());
  MOCK_METHOD0(statPrefix, const std::string&());
};

class MockIp : public Address::Ip {
public:
  MockIp();
  ~MockIp() override;

  MOCK_CONST_METHOD0(addressAsString, const std::string&());
  MOCK_CONST_METHOD0(isAnyAddress, bool());
  MOCK_CONST_METHOD0(isUnicastAddress, bool());
  MOCK_CONST_METHOD0(ipv4, Address::Ipv4*());
  MOCK_CONST_METHOD0(ipv6, Address::Ipv6*());
  MOCK_CONST_METHOD0(port, uint32_t());
  MOCK_CONST_METHOD0(version, Address::IpVersion());
};

class MockResolvedAddress : public Address::Instance {
public:
  MockResolvedAddress(const std::string& logical, const std::string& physical)
      : logical_(logical), physical_(physical) {}
  ~MockResolvedAddress() override;

  bool operator==(const Address::Instance& other) const override {
    return asString() == other.asString();
  }

  MOCK_CONST_METHOD1(bind, Api::SysCallIntResult(int));
  MOCK_CONST_METHOD1(connect, Api::SysCallIntResult(int));
  MOCK_CONST_METHOD0(ip, Address::Ip*());
  MOCK_CONST_METHOD1(socket, IoHandlePtr(Address::SocketType));
  MOCK_CONST_METHOD0(type, Address::Type());
  MOCK_CONST_METHOD0(sockAddr, sockaddr*());
  MOCK_CONST_METHOD0(sockAddrLen, socklen_t());

  const std::string& asString() const override { return physical_; }
  absl::string_view asStringView() const override { return physical_; }
  const std::string& logicalName() const override { return logical_; }

  const std::string logical_;
  const std::string physical_;
};

class MockTransportSocketCallbacks : public TransportSocketCallbacks {
public:
  MockTransportSocketCallbacks();
  ~MockTransportSocketCallbacks() override;

  MOCK_METHOD0(ioHandle, IoHandle&());
  MOCK_CONST_METHOD0(ioHandle, const IoHandle&());
  MOCK_METHOD0(connection, Connection&());
  MOCK_METHOD0(shouldDrainReadBuffer, bool());
  MOCK_METHOD0(setReadBufferReady, void());
  MOCK_METHOD1(raiseEvent, void(ConnectionEvent));

  testing::NiceMock<MockConnection> connection_;
};

class MockUdpListener : public UdpListener {
public:
  MockUdpListener();
  ~MockUdpListener() override;

  MOCK_METHOD0(onDestroy, void());
  MOCK_METHOD0(enable, void());
  MOCK_METHOD0(disable, void());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_CONST_METHOD0(localAddress, Address::InstanceConstSharedPtr&());
  MOCK_METHOD1(send, Api::IoCallUint64Result(const UdpSendData&));
};

class MockUdpReadFilterCallbacks : public UdpReadFilterCallbacks {
public:
  MockUdpReadFilterCallbacks();
  ~MockUdpReadFilterCallbacks() override;

  MOCK_METHOD0(udpListener, UdpListener&());

  testing::NiceMock<MockUdpListener> udp_listener_;
};

class MockUdpListenerReadFilter : public UdpListenerReadFilter {
public:
  MockUdpListenerReadFilter(UdpReadFilterCallbacks& callbacks);
  ~MockUdpListenerReadFilter() override;

  MOCK_METHOD1(onData, void(UdpRecvData&));
};

class MockUdpListenerFilterManager : public UdpListenerFilterManager {
public:
  MockUdpListenerFilterManager();
  ~MockUdpListenerFilterManager() override;

  void addReadFilter(UdpListenerReadFilterPtr&& filter) override { addReadFilter_(filter); }

  MOCK_METHOD1(addReadFilter_, void(Network::UdpListenerReadFilterPtr&));
};

class MockConnectionBalancer : public ConnectionBalancer {
public:
  MockConnectionBalancer();
  ~MockConnectionBalancer() override;

  MOCK_METHOD1(registerHandler, void(BalancedConnectionHandler& handler));
  MOCK_METHOD1(unregisterHandler, void(BalancedConnectionHandler& handler));
  MOCK_METHOD1(pickTargetHandler,
               BalancedConnectionHandler&(BalancedConnectionHandler& current_handler));
};

} // namespace Network
} // namespace Envoy
