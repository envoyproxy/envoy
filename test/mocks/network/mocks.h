#pragma once

#include <algorithm>
#include <cstdint>
#include <list>
#include <ostream>
#include <string>
#include <vector>

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/udp_listener_config.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/network/resolver.h"
#include "envoy/network/socket.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/scope.h"

#include "source/common/network/dns_resolver/dns_factory_util.h"
#include "source/common/network/filter_manager_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/socket_interface_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stream_info/filter_state_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/network/io_handle.h"
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
  MOCK_METHOD(void, cancel, (CancelReason reason));
  MOCK_METHOD(void, addTrace, (uint8_t));
  MOCK_METHOD(std::string, getTraces, ());
};

class MockFilterManager : public FilterManager {
public:
  MOCK_METHOD(void, addWriteFilter, (WriteFilterSharedPtr filter));
  MOCK_METHOD(void, addFilter, (FilterSharedPtr filter));
  MOCK_METHOD(void, addReadFilter, (ReadFilterSharedPtr filter));
  MOCK_METHOD(void, removeReadFilter, (ReadFilterSharedPtr filter));
  MOCK_METHOD(bool, initializeReadFilters, ());
};

class MockDnsResolver : public DnsResolver {
public:
  MockDnsResolver();
  ~MockDnsResolver() override;

  // Network::DnsResolver
  MOCK_METHOD(ActiveDnsQuery*, resolve,
              (const std::string& dns_name, DnsLookupFamily dns_lookup_family, ResolveCb callback));
  MOCK_METHOD(void, resetNetworking, ());

  testing::NiceMock<MockActiveDnsQuery> active_query_;
};

class MockDnsResolverFactory : public DnsResolverFactory {
public:
  MockDnsResolverFactory() = default;
  ~MockDnsResolverFactory() override = default;

  MOCK_METHOD(absl::StatusOr<DnsResolverSharedPtr>, createDnsResolver,
              (Event::Dispatcher & dispatcher, Api::Api& api,
               const envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config),
              (const, override));
  std::string name() const override { return std::string(CaresDnsResolver); };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig()};
  }
};

class MockAddressResolver : public Address::Resolver {
public:
  MockAddressResolver();
  ~MockAddressResolver() override;

  MOCK_METHOD(absl::StatusOr<Address::InstanceConstSharedPtr>, resolve,
              (const envoy::config::core::v3::SocketAddress&));
  MOCK_METHOD(std::string, name, (), (const));
};

class MockReadFilterCallbacks : public ReadFilterCallbacks {
public:
  MockReadFilterCallbacks();
  ~MockReadFilterCallbacks() override;

  MOCK_METHOD(Connection&, connection, ());
  MOCK_METHOD(const Socket&, socket, ());
  MOCK_METHOD(void, continueReading, ());
  MOCK_METHOD(void, injectReadDataToFilterChain, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(Upstream::HostDescriptionConstSharedPtr, upstreamHost, ());
  MOCK_METHOD(void, upstreamHost, (Upstream::HostDescriptionConstSharedPtr host));
  MOCK_METHOD(bool, startUpstreamSecureTransport, ());

  testing::NiceMock<MockConnection> connection_;
  Upstream::HostDescriptionConstSharedPtr host_;
};

class MockReadFilter : public ReadFilter {
public:
  MockReadFilter();
  ~MockReadFilter() override;

  MOCK_METHOD(FilterStatus, onData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(FilterStatus, onNewConnection, ());
  MOCK_METHOD(void, initializeReadFilterCallbacks, (ReadFilterCallbacks & callbacks));
  MOCK_METHOD(bool, startUpstreamSecureTransport, ());

  ReadFilterCallbacks* callbacks_{};
};

class MockWriteFilterCallbacks : public WriteFilterCallbacks {
public:
  MockWriteFilterCallbacks();
  ~MockWriteFilterCallbacks() override;

  MOCK_METHOD(Connection&, connection, ());
  MOCK_METHOD(const Socket&, socket, ());
  MOCK_METHOD(void, injectWriteDataToFilterChain, (Buffer::Instance & data, bool end_stream));

  testing::NiceMock<MockConnection> connection_;
};

class MockWriteFilter : public WriteFilter {
public:
  MockWriteFilter();
  ~MockWriteFilter() override;

  MOCK_METHOD(FilterStatus, onWrite, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, initializeWriteFilterCallbacks, (WriteFilterCallbacks & callbacks));

  WriteFilterCallbacks* write_callbacks_{};
};

class MockFilter : public Filter {
public:
  MockFilter();
  ~MockFilter() override;

  MOCK_METHOD(FilterStatus, onData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(FilterStatus, onNewConnection, ());
  MOCK_METHOD(FilterStatus, onWrite, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, initializeReadFilterCallbacks, (ReadFilterCallbacks & callbacks));
  MOCK_METHOD(void, initializeWriteFilterCallbacks, (WriteFilterCallbacks & callbacks));

  ReadFilterCallbacks* callbacks_{};
  WriteFilterCallbacks* write_callbacks_{};
};

class MockTcpListenerCallbacks : public TcpListenerCallbacks {
public:
  MockTcpListenerCallbacks();
  ~MockTcpListenerCallbacks() override;

  void onAccept(ConnectionSocketPtr&& socket) override { onAccept_(socket); }

  MOCK_METHOD(void, onAccept_, (ConnectionSocketPtr & socket));
  MOCK_METHOD(void, onReject, (RejectCause), (override));
  MOCK_METHOD(void, recordConnectionsAcceptedOnSocketEvent, (uint32_t), (override));
};

class MockUdpListenerCallbacks : public UdpListenerCallbacks {
public:
  MockUdpListenerCallbacks();
  ~MockUdpListenerCallbacks() override;

  MOCK_METHOD(void, onData, (UdpRecvData && data));
  MOCK_METHOD(void, onDatagramsDropped, (uint32_t dropped));
  MOCK_METHOD(void, onReadReady, ());
  MOCK_METHOD(void, onWriteReady, (const Socket& socket));
  MOCK_METHOD(void, onReceiveError, (Api::IoError::IoErrorCode err));
  MOCK_METHOD(Network::UdpPacketWriter&, udpPacketWriter, ());
  MOCK_METHOD(uint32_t, workerIndex, (), (const));
  MOCK_METHOD(void, onDataWorker, (Network::UdpRecvData && data));
  MOCK_METHOD(void, post, (Network::UdpRecvData && data));
  MOCK_METHOD(size_t, numPacketsExpectedPerEventLoop, (), (const));
  MOCK_METHOD(const IoHandle::UdpSaveCmsgConfig&, udpSaveCmsgConfig, (), (const));
};

class MockDrainDecision : public DrainDecision {
public:
  MockDrainDecision();
  ~MockDrainDecision() override;

  MOCK_METHOD(bool, drainClose, (), (const));
  MOCK_METHOD(Common::CallbackHandlePtr, addOnDrainCloseCb, (DrainCloseCb cb), (const, override));
};

class MockListenerFilter : public ListenerFilter {
public:
  MockListenerFilter(size_t max_read_bytes = 0) : listener_filter_max_read_bytes_(max_read_bytes) {}
  ~MockListenerFilter() override;

  size_t maxReadBytes() const override { return listener_filter_max_read_bytes_; }

  MOCK_METHOD(void, destroy_, ());
  MOCK_METHOD(Network::FilterStatus, onAccept, (ListenerFilterCallbacks&));
  MOCK_METHOD(Network::FilterStatus, onData, (Network::ListenerFilterBuffer&));

  size_t listener_filter_max_read_bytes_{0};
};

class MockListenerFilterManager : public ListenerFilterManager {
public:
  MockListenerFilterManager();
  ~MockListenerFilterManager() override;

  void addAcceptFilter(const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
                       ListenerFilterPtr&& filter) override {
    addAcceptFilter_(listener_filter_matcher, filter);
  }

  MOCK_METHOD(void, addAcceptFilter_,
              (const Network::ListenerFilterMatcherSharedPtr&, Network::ListenerFilterPtr&));
};

#ifdef ENVOY_ENABLE_QUIC

} // namespace Network
} // namespace Envoy

#include "quiche/quic/platform/api/quic_socket_address.h"

namespace Envoy {
namespace Network {

class MockQuicListenerFilter : public QuicListenerFilter {
public:
  MOCK_METHOD(Network::FilterStatus, onAccept, (ListenerFilterCallbacks&));
  MOCK_METHOD(bool, shouldAdvertiseServerPreferredAddress, (const quic::QuicSocketAddress&),
              (const));
  MOCK_METHOD(Network::FilterStatus, onPeerAddressChanged,
              (const quic::QuicSocketAddress&, Network::Connection&));
};

class MockQuicListenerFilterManager : public QuicListenerFilterManager {
public:
  MOCK_METHOD(void, addFilter,
              (const Network::ListenerFilterMatcherSharedPtr&, QuicListenerFilterPtr&&));
  MOCK_METHOD(bool, shouldAdvertiseServerPreferredAddress, (const quic::QuicSocketAddress&),
              (const));

  MOCK_METHOD(void, onPeerAddressChanged, (const quic::QuicSocketAddress&, Connection&));
  MOCK_METHOD(void, onFirstPacketReceived, (const quic::QuicReceivedPacket&));
};

#endif

class MockFilterChain : public DrainableFilterChain {
public:
  MockFilterChain();
  ~MockFilterChain() override;

  // Network::DrainableFilterChain
  MOCK_METHOD(const DownstreamTransportSocketFactory&, transportSocketFactory, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, transportSocketConnectTimeout, (), (const));
  MOCK_METHOD(const NetworkFilterFactoriesList&, networkFilterFactories, (), (const));
  MOCK_METHOD(void, startDraining, ());
  MOCK_METHOD(absl::string_view, name, (), (const));
};

class MockFilterChainInfo : public FilterChainInfo {
public:
  MockFilterChainInfo();

  // Network::FilterChainInfo
  MOCK_METHOD(absl::string_view, name, (), (const));

  std::string filter_chain_name_{"mock"};
};

class MockFilterChainManager : public FilterChainManager {
public:
  MockFilterChainManager();
  ~MockFilterChainManager() override;

  // Network::FilterChainManager
  MOCK_METHOD(const FilterChain*, findFilterChain,
              (const ConnectionSocket& socket, const StreamInfo::StreamInfo& info), (const));
};

class MockFilterChainFactory : public FilterChainFactory {
public:
  MockFilterChainFactory();
  ~MockFilterChainFactory() override;

  MOCK_METHOD(bool, createNetworkFilterChain,
              (Connection & connection, const NetworkFilterFactoriesList& filter_factories));
  MOCK_METHOD(bool, createListenerFilterChain, (ListenerFilterManager & listener));
  MOCK_METHOD(void, createUdpListenerFilterChain,
              (UdpListenerFilterManager & listener, UdpReadFilterCallbacks& callbacks));
  MOCK_METHOD(bool, createQuicListenerFilterChain, (QuicListenerFilterManager & listener));
};

class MockListenSocket : public Socket {
public:
  MockListenSocket();
  ~MockListenSocket() override = default;

  void addOption(const Socket::OptionConstSharedPtr& option) override { addOption_(option); }
  void addOptions(const Socket::OptionsSharedPtr& options) override { addOptions_(options); }

  ConnectionInfoSetter& connectionInfoProvider() override { return *connection_info_provider_; }
  const ConnectionInfoProvider& connectionInfoProvider() const override {
    return *connection_info_provider_;
  }
  ConnectionInfoProviderSharedPtr connectionInfoProviderSharedPtr() const override {
    return connection_info_provider_;
  }
  MOCK_METHOD(IoHandle&, ioHandle, ());
  MOCK_METHOD(SocketPtr, duplicate, ());
  MOCK_METHOD(const IoHandle&, ioHandle, (), (const));
  MOCK_METHOD(Socket::Type, socketType, (), (const));
  MOCK_METHOD(Address::Type, addressType, (), (const));
  MOCK_METHOD(absl::optional<Address::IpVersion>, ipVersion, (), (const));
  MOCK_METHOD(void, close, ());
  MOCK_METHOD(bool, isOpen, (), (const));
  MOCK_METHOD(void, addOption_, (const Socket::OptionConstSharedPtr& option));
  MOCK_METHOD(void, addOptions_, (const Socket::OptionsSharedPtr& options));
  MOCK_METHOD(const OptionsSharedPtr&, options, (), (const));
  MOCK_METHOD(IoHandlePtr, socket, (Socket::Type, Address::Type, Address::IpVersion), (const));
  MOCK_METHOD(IoHandlePtr, socketForAddrPtr, (Socket::Type, const Address::InstanceConstSharedPtr),
              (const));
  MOCK_METHOD(Api::SysCallIntResult, bind, (const Address::InstanceConstSharedPtr));
  MOCK_METHOD(Api::SysCallIntResult, connect, (const Address::InstanceConstSharedPtr));
  MOCK_METHOD(Api::SysCallIntResult, listen, (int));
  MOCK_METHOD(Api::SysCallIntResult, setSocketOption, (int, int, const void*, socklen_t));
  MOCK_METHOD(Api::SysCallIntResult, getSocketOption, (int, int, void*, socklen_t*), (const));
  MOCK_METHOD(Api::SysCallIntResult, ioctl,
              (unsigned long, void*, unsigned long, void*, unsigned long, unsigned long*));
  MOCK_METHOD(Api::SysCallIntResult, setBlockingForTest, (bool));

  std::unique_ptr<MockIoHandle> io_handle_;
  Network::ConnectionInfoSetterSharedPtr connection_info_provider_;
  OptionsSharedPtr options_;
  bool socket_is_open_ = true;
};

class MockSocketOption : public Socket::Option {
public:
  MockSocketOption();
  ~MockSocketOption() override;

  MOCK_METHOD(bool, setOption, (Socket&, envoy::config::core::v3::SocketOption::SocketState state),
              (const));
  MOCK_METHOD(void, hashKey, (std::vector<uint8_t>&), (const));
  MOCK_METHOD(absl::optional<Socket::Option::Details>, getOptionDetails,
              (const Socket&, envoy::config::core::v3::SocketOption::SocketState state), (const));
  MOCK_METHOD(bool, isSupported, (), (const));
};

class MockConnectionSocket : public ConnectionSocket {
public:
  MockConnectionSocket();
  ~MockConnectionSocket() override;

  void addOption(const Socket::OptionConstSharedPtr& option) override { addOption_(option); }
  void addOptions(const Socket::OptionsSharedPtr& options) override { addOptions_(options); }

  ConnectionInfoSetter& connectionInfoProvider() override { return *connection_info_provider_; }
  const ConnectionInfoProvider& connectionInfoProvider() const override {
    return *connection_info_provider_;
  }
  ConnectionInfoProviderSharedPtr connectionInfoProviderSharedPtr() const override {
    return connection_info_provider_;
  }
  MOCK_METHOD(void, setDetectedTransportProtocol, (absl::string_view));
  MOCK_METHOD(absl::string_view, detectedTransportProtocol, (), (const));
  MOCK_METHOD(void, setRequestedApplicationProtocols, (const std::vector<absl::string_view>&));
  MOCK_METHOD(const std::vector<std::string>&, requestedApplicationProtocols, (), (const));
  MOCK_METHOD(void, setRequestedServerName, (absl::string_view));
  MOCK_METHOD(absl::string_view, requestedServerName, (), (const));
  MOCK_METHOD(void, setJA3Hash, (absl::string_view));
  MOCK_METHOD(absl::string_view, ja3Hash, (), (const));
  MOCK_METHOD(void, addOption_, (const Socket::OptionConstSharedPtr&));
  MOCK_METHOD(void, addOptions_, (const Socket::OptionsSharedPtr&));
  MOCK_METHOD(const Network::ConnectionSocket::OptionsSharedPtr&, options, (), (const));
  MOCK_METHOD(SocketPtr, duplicate, ());
  MOCK_METHOD(IoHandle&, ioHandle, ());
  MOCK_METHOD(const IoHandle&, ioHandle, (), (const));
  MOCK_METHOD(Socket::Type, socketType, (), (const));
  MOCK_METHOD(Address::Type, addressType, (), (const));
  MOCK_METHOD(absl::optional<Address::IpVersion>, ipVersion, (), (const));
  MOCK_METHOD(void, close, ());
  MOCK_METHOD(bool, isOpen, (), (const));
  MOCK_METHOD(IoHandlePtr, socket, (Socket::Type, Address::Type, Address::IpVersion), (const));
  MOCK_METHOD(IoHandlePtr, socketForAddrPtr, (Socket::Type, const Address::InstanceConstSharedPtr),
              (const));
  MOCK_METHOD(Api::SysCallIntResult, bind, (const Address::InstanceConstSharedPtr));
  MOCK_METHOD(Api::SysCallIntResult, connect, (const Address::InstanceConstSharedPtr));
  MOCK_METHOD(Api::SysCallIntResult, listen, (int));
  MOCK_METHOD(Api::SysCallIntResult, setSocketOption, (int, int, const void*, socklen_t));
  MOCK_METHOD(Api::SysCallIntResult, getSocketOption, (int, int, void*, socklen_t*), (const));
  MOCK_METHOD(Api::SysCallIntResult, ioctl,
              (unsigned long, void*, unsigned long, void*, unsigned long, unsigned long*));
  MOCK_METHOD(Api::SysCallIntResult, setBlockingForTest, (bool));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, lastRoundTripTime, ());
  MOCK_METHOD(absl::optional<uint64_t>, congestionWindowInBytes, (), (const));
  MOCK_METHOD(void, dumpState, (std::ostream&, int), (const));

  IoHandlePtr io_handle_;
  std::shared_ptr<Network::ConnectionInfoSetterImpl> connection_info_provider_;
};

class MockListenerFilterCallbacks : public ListenerFilterCallbacks {
public:
  MockListenerFilterCallbacks();
  ~MockListenerFilterCallbacks() override;

  MOCK_METHOD(ConnectionSocket&, socket, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(void, continueFilterChain, (bool));
  MOCK_METHOD(void, setDynamicMetadata, (const std::string&, const ProtobufWkt::Struct&));
  MOCK_METHOD(void, setDynamicTypedMetadata, (const std::string&, const ProtobufWkt::Any& value));
  MOCK_METHOD(envoy::config::core::v3::Metadata&, dynamicMetadata, ());
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, dynamicMetadata, (), (const));
  MOCK_METHOD(StreamInfo::FilterState&, filterState, (), ());
  MOCK_METHOD(void, useOriginalDst, (bool));

  StreamInfo::FilterStateImpl filter_state_;
  NiceMock<MockConnectionSocket> socket_;
};

class MockListenSocketFactory : public ListenSocketFactory {
public:
  MockListenSocketFactory() = default;

  MOCK_METHOD(Network::Socket::Type, socketType, (), (const));
  MOCK_METHOD(const Network::Address::InstanceConstSharedPtr&, localAddress, (), (const));
  MOCK_METHOD(Network::SocketSharedPtr, getListenSocket, (uint32_t));
  MOCK_METHOD(bool, reusePort, (), (const));
  MOCK_METHOD(Network::ListenSocketFactoryPtr, clone, (), (const));
  MOCK_METHOD(void, closeAllSockets, ());
  MOCK_METHOD(absl::Status, doFinalPreWorkerInit, ());
};

class MockUdpPacketWriterFactory : public UdpPacketWriterFactory {
public:
  MockUdpPacketWriterFactory() = default;

  MOCK_METHOD(Network::UdpPacketWriterPtr, createUdpPacketWriter,
              (Network::IoHandle&, Stats::Scope&), ());
};

class MockUdpListenerConfig : public UdpListenerConfig {
public:
  MockUdpListenerConfig(uint32_t concurrency = 1);
  ~MockUdpListenerConfig() override;

  MOCK_METHOD(ActiveUdpListenerFactory&, listenerFactory, ());
  MOCK_METHOD(UdpPacketWriterFactory&, packetWriterFactory, ());
  MOCK_METHOD(UdpListenerWorkerRouter&, listenerWorkerRouter, (const Network::Address::Instance&));
  MOCK_METHOD(const envoy::config::listener::v3::UdpListenerConfig&, config, ());

  UdpListenerWorkerRouterPtr udp_listener_worker_router_;
  envoy::config::listener::v3::UdpListenerConfig config_;
};

class MockListenerInfo : public ListenerInfo {
public:
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, metadata, (), (const));
  MOCK_METHOD(const Envoy::Config::TypedMetadata&, typedMetadata, (), (const));
  MOCK_METHOD(envoy::config::core::v3::TrafficDirection, direction, (), (const));
  MOCK_METHOD(bool, isQuic, (), (const));
  MOCK_METHOD(bool, shouldBypassOverloadManager, (), (const));
};

class MockListenerConfig : public ListenerConfig {
public:
  MockListenerConfig();
  ~MockListenerConfig() override;

  MOCK_METHOD(FilterChainManager&, filterChainManager, ());
  MOCK_METHOD(FilterChainFactory&, filterChainFactory, ());
  MOCK_METHOD(std::vector<ListenSocketFactoryPtr>&, listenSocketFactories, ());
  MOCK_METHOD(bool, bindToPort, (), (const));
  MOCK_METHOD(bool, handOffRestoredDestinationConnections, (), (const));
  MOCK_METHOD(uint32_t, perConnectionBufferLimitBytes, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, listenerFiltersTimeout, (), (const));
  MOCK_METHOD(bool, continueOnListenerFiltersTimeout, (), (const));
  MOCK_METHOD(Stats::Scope&, listenerScope, ());
  MOCK_METHOD(uint64_t, listenerTag, (), (const));
  MOCK_METHOD(const std::string&, name, (), (const));
  MOCK_METHOD(Network::UdpListenerConfigOptRef, udpListenerConfig, ());
  MOCK_METHOD(InternalListenerConfigOptRef, internalListenerConfig, ());
  MOCK_METHOD(ConnectionBalancer&, connectionBalancer, (const Network::Address::Instance&));
  MOCK_METHOD(ResourceLimit&, openConnections, ());
  MOCK_METHOD(uint32_t, tcpBacklogSize, (), (const));
  MOCK_METHOD(uint32_t, maxConnectionsToAcceptPerSocketEvent, (), (const));
  MOCK_METHOD(Init::Manager&, initManager, ());
  MOCK_METHOD(bool, ignoreGlobalConnLimit, (), (const));
  MOCK_METHOD(bool, shouldBypassOverloadManager, (), (const));

  const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const override {
    return empty_access_logs_;
  }

  const ListenerInfoConstSharedPtr& listenerInfo() const override { return listener_info_; }

  testing::NiceMock<MockFilterChainFactory> filter_chain_factory_;
  std::vector<ListenSocketFactoryPtr> socket_factories_;
  SocketSharedPtr socket_;
  ListenerInfoConstSharedPtr listener_info_;
  Stats::IsolatedStoreImpl store_;
  std::string name_;
  const std::vector<AccessLog::InstanceSharedPtr> empty_access_logs_;
};

class MockListener : public Listener {
public:
  MockListener();
  ~MockListener() override;

  MOCK_METHOD(void, onDestroy, ());
  MOCK_METHOD(void, enable, ());
  MOCK_METHOD(void, disable, ());
  MOCK_METHOD(void, setRejectFraction, (UnitFloat));
  MOCK_METHOD(void, configureLoadShedPoints, (Server::LoadShedPointProvider&));
  MOCK_METHOD(bool, shouldBypassOverloadManager, (), (const));
};

class MockConnectionHandler : public virtual ConnectionHandler {
public:
  MockConnectionHandler();
  ~MockConnectionHandler() override;

  MOCK_METHOD(uint64_t, numConnections, (), (const));
  MOCK_METHOD(void, incNumConnections, ());
  MOCK_METHOD(void, decNumConnections, ());
  MOCK_METHOD(void, addListener,
              (absl::optional<uint64_t> overridden_listener, ListenerConfig& config,
               Runtime::Loader& runtime, Random::RandomGenerator& random));
  MOCK_METHOD(void, removeListeners, (uint64_t listener_tag));
  MOCK_METHOD(void, removeFilterChains,
              (uint64_t listener_tag, const std::list<const Network::FilterChain*>& filter_chains,
               std::function<void()> completion));
  MOCK_METHOD(void, stopListeners,
              (uint64_t listener_tag, const Network::ExtraShutdownListenerOptions& options));
  MOCK_METHOD(void, stopListeners, ());
  MOCK_METHOD(void, disableListeners, ());
  MOCK_METHOD(void, enableListeners, ());
  MOCK_METHOD(void, setListenerRejectFraction, (UnitFloat), (override));
  MOCK_METHOD(const std::string&, statPrefix, (), (const));

  uint64_t num_handler_connections_{};
};

class MockUdpListenerWorkerRouter : public UdpListenerWorkerRouter {
public:
  ~MockUdpListenerWorkerRouter() override;

  MOCK_METHOD(void, registerWorkerForListener, (UdpListenerCallbacks & listener));
  MOCK_METHOD(void, unregisterWorkerForListener, (UdpListenerCallbacks & listener));
  MOCK_METHOD(void, deliver, (uint32_t dest_worker_index, UdpRecvData&& data));
};

class MockIp : public Address::Ip {
public:
  MockIp();
  ~MockIp() override;

  MOCK_METHOD(const std::string&, addressAsString, (), (const));
  MOCK_METHOD(bool, isAnyAddress, (), (const));
  MOCK_METHOD(bool, isUnicastAddress, (), (const));
  MOCK_METHOD(const Address::Ipv4*, ipv4, (), (const));
  MOCK_METHOD(const Address::Ipv6*, ipv6, (), (const));
  MOCK_METHOD(uint32_t, port, (), (const));
  MOCK_METHOD(Address::IpVersion, version, (), (const));
  MOCK_METHOD(bool, v6only, (), (const));
};

class MockResolvedAddress : public Address::Instance {
public:
  MockResolvedAddress(const std::string& logical, const std::string& physical);
  ~MockResolvedAddress() override;

  bool operator==(const Address::Instance& other) const override {
    return asString() == other.asString();
  }

  MOCK_METHOD(Api::SysCallIntResult, bind, (os_fd_t), (const));
  MOCK_METHOD(Api::SysCallIntResult, connect, (os_fd_t), (const));
  MOCK_METHOD(const Address::Ip*, ip, (), (const));
  MOCK_METHOD(const Address::Pipe*, pipe, (), (const));
  MOCK_METHOD(Address::EnvoyInternalAddress*, envoyInternalAddress, (), (const));
  MOCK_METHOD(IoHandlePtr, socket, (Socket::Type), (const));
  MOCK_METHOD(Address::Type, type, (), (const));
  MOCK_METHOD(const sockaddr*, sockAddr, (), (const));
  MOCK_METHOD(socklen_t, sockAddrLen, (), (const));
  MOCK_METHOD(absl::string_view, addressType, (), (const));

  const std::string& asString() const override { return physical_; }
  absl::string_view asStringView() const override { return physical_; }
  const std::string& logicalName() const override { return logical_; }
  const Network::SocketInterface& socketInterface() const override {
    return SocketInterfaceSingleton::get();
  }

  const std::string logical_;
  const std::string physical_;
};

class MockTransportSocketCallbacks : public TransportSocketCallbacks {
public:
  MockTransportSocketCallbacks();
  ~MockTransportSocketCallbacks() override;

  MOCK_METHOD(IoHandle&, ioHandle, ());
  MOCK_METHOD(const IoHandle&, ioHandle, (), (const));
  MOCK_METHOD(Connection&, connection, ());
  MOCK_METHOD(bool, shouldDrainReadBuffer, ());
  MOCK_METHOD(void, setTransportSocketIsReadable, ());
  MOCK_METHOD(void, raiseEvent, (ConnectionEvent));
  MOCK_METHOD(void, flushWriteBuffer, ());

  testing::NiceMock<MockConnection> connection_;
};

class MockUdpPacketWriter : public UdpPacketWriter {
public:
  MockUdpPacketWriter();
  ~MockUdpPacketWriter() override;

  MOCK_METHOD(Api::IoCallUint64Result, writePacket,
              (const Buffer::Instance& buffer, const Address::Ip* local_ip,
               const Address::Instance& peer_address));
  MOCK_METHOD(bool, isWriteBlocked, (), (const));
  MOCK_METHOD(void, setWritable, ());
  MOCK_METHOD(uint64_t, getMaxPacketSize, (const Address::Instance& peer_address), (const));
  MOCK_METHOD(bool, isBatchMode, (), (const));
  MOCK_METHOD(Network::UdpPacketWriterBuffer, getNextWriteLocation,
              (const Address::Ip* local_ip, const Address::Instance& peer_address));
  MOCK_METHOD(Api::IoCallUint64Result, flush, ());
};

class MockUdpListener : public UdpListener {
public:
  MockUdpListener();
  ~MockUdpListener() override;

  MOCK_METHOD(void, onDestroy, ());
  MOCK_METHOD(void, enable, ());
  MOCK_METHOD(void, disable, ());
  MOCK_METHOD(void, setRejectFraction, (UnitFloat), (override));
  MOCK_METHOD(void, configureLoadShedPoints, (Server::LoadShedPointProvider&));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(Address::InstanceConstSharedPtr&, localAddress, (), (const));
  MOCK_METHOD(Api::IoCallUint64Result, send, (const UdpSendData&));
  MOCK_METHOD(Api::IoCallUint64Result, flush, ());
  MOCK_METHOD(void, activateRead, ());
  MOCK_METHOD(bool, shouldBypassOverloadManager, (), (const));

  Event::MockDispatcher dispatcher_;
};

class MockUdpReadFilterCallbacks : public UdpReadFilterCallbacks {
public:
  MockUdpReadFilterCallbacks();
  ~MockUdpReadFilterCallbacks() override;

  MOCK_METHOD(UdpListener&, udpListener, ());

  testing::NiceMock<MockUdpListener> udp_listener_;
};

class MockUdpListenerReadFilter : public UdpListenerReadFilter {
public:
  MockUdpListenerReadFilter(UdpReadFilterCallbacks& callbacks);
  ~MockUdpListenerReadFilter() override;

  MOCK_METHOD(Network::FilterStatus, onData, (UdpRecvData&));
  MOCK_METHOD(Network::FilterStatus, onReceiveError, (Api::IoError::IoErrorCode));
};

class MockUdpListenerFilterManager : public UdpListenerFilterManager {
public:
  MockUdpListenerFilterManager();
  ~MockUdpListenerFilterManager() override;

  void addReadFilter(UdpListenerReadFilterPtr&& filter) override { addReadFilter_(filter); }

  MOCK_METHOD(void, addReadFilter_, (Network::UdpListenerReadFilterPtr&));
};

class MockConnectionBalancer : public ConnectionBalancer {
public:
  MockConnectionBalancer();
  ~MockConnectionBalancer() override;

  MOCK_METHOD(void, registerHandler, (BalancedConnectionHandler & handler));
  MOCK_METHOD(void, unregisterHandler, (BalancedConnectionHandler & handler));
  MOCK_METHOD(BalancedConnectionHandler&, pickTargetHandler,
              (BalancedConnectionHandler & current_handler));
};

class MockListenerFilterMatcher : public ListenerFilterMatcher {
public:
  MockListenerFilterMatcher();
  ~MockListenerFilterMatcher() override;
  MOCK_METHOD(bool, matches, (Network::ListenerFilterCallbacks & cb), (const));
};

class MockUdpPacketProcessor : public UdpPacketProcessor {
public:
  MOCK_METHOD(void, processPacket,
              (Address::InstanceConstSharedPtr local_address,
               Address::InstanceConstSharedPtr peer_address, Buffer::InstancePtr buffer,
               MonotonicTime receive_time, uint8_t tos, Buffer::RawSlice saved_cmsg));
  MOCK_METHOD(void, onDatagramsDropped, (uint32_t dropped));
  MOCK_METHOD(uint64_t, maxDatagramSize, (), (const));
  MOCK_METHOD(size_t, numPacketsExpectedPerEventLoop, (), (const));
  MOCK_METHOD(const IoHandle::UdpSaveCmsgConfig&, saveCmsgConfig, (), (const));
};

class MockSocketInterface : public SocketInterfaceImpl {
public:
  explicit MockSocketInterface(const std::vector<Address::IpVersion>& versions)
      : versions_(versions.begin(), versions.end()) {}
  MOCK_METHOD(IoHandlePtr, socket,
              (Socket::Type, Address::Type, Address::IpVersion, bool, const SocketCreationOptions&),
              (const));
  MOCK_METHOD(IoHandlePtr, socket,
              (Socket::Type, const Address::InstanceConstSharedPtr, const SocketCreationOptions&),
              (const));
  bool ipFamilySupported(int domain) override {
    const auto to_version = domain == AF_INET ? Address::IpVersion::v4 : Address::IpVersion::v6;
    return std::any_of(versions_.begin(), versions_.end(),
                       [to_version](auto version) { return to_version == version; });
  }
  const std::vector<Address::IpVersion> versions_;
};

} // namespace Network
} // namespace Envoy
