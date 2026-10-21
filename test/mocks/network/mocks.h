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
#include "envoy/network/listen_socket.h"
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
  MOCK_METHOD(void, cancel, (CancelReason reason), (override));
  MOCK_METHOD(void, addTrace, (uint8_t), (override));
  MOCK_METHOD(std::string, getTraces, (), (override));
};

class MockFilterManager : public FilterManager {
public:
  MOCK_METHOD(void, addWriteFilter, (WriteFilterSharedPtr filter), (override));
  MOCK_METHOD(void, addFilter, (FilterSharedPtr filter), (override));
  MOCK_METHOD(void, addReadFilter, (ReadFilterSharedPtr filter), (override));
  MOCK_METHOD(void, removeReadFilter, (ReadFilterSharedPtr filter), (override));
  MOCK_METHOD(bool, initializeReadFilters, (), (override));
  MOCK_METHOD(void, addAccessLogHandler, (AccessLog::InstanceSharedPtr handler), (override));
};

class MockDnsResolver : public DnsResolver {
public:
  MockDnsResolver();
  ~MockDnsResolver() override;

  // Network::DnsResolver
  MOCK_METHOD(ActiveDnsQuery*, resolve,
              (const std::string& dns_name, DnsLookupFamily dns_lookup_family, ResolveCb callback),
              (override));
  MOCK_METHOD(void, resetNetworking, (), (override));

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
              (const envoy::config::core::v3::SocketAddress&), (override));
  MOCK_METHOD(std::string, name, (), (const, override));
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
  MOCK_METHOD(void, setDetectedTransportProtocol, (absl::string_view), (override));
  MOCK_METHOD(absl::string_view, detectedTransportProtocol, (), (const, override));
  MOCK_METHOD(void, setRequestedApplicationProtocols, (const std::vector<absl::string_view>&),
              (override));
  MOCK_METHOD(const std::vector<std::string>&, requestedApplicationProtocols, (),
              (const, override));
  MOCK_METHOD(void, setRequestedServerName, (absl::string_view), (override));
  MOCK_METHOD(absl::string_view, requestedServerName, (), (const, override));
  MOCK_METHOD(void, setJA3Hash, (absl::string_view), (override));
  MOCK_METHOD(absl::string_view, ja3Hash, (), (const, override));
  MOCK_METHOD(void, setJA4Hash, (absl::string_view ja4_hash), (override));
  MOCK_METHOD(absl::string_view, ja4Hash, (), (const, override));
  MOCK_METHOD(void, addOption_, (const Socket::OptionConstSharedPtr&));
  MOCK_METHOD(void, addOptions_, (const Socket::OptionsSharedPtr&));
  MOCK_METHOD(const Network::ConnectionSocket::OptionsSharedPtr&, options, (), (const, override));
  MOCK_METHOD(SocketPtr, duplicate, (), (override));
  MOCK_METHOD(IoHandle&, ioHandle, (), (override));
  MOCK_METHOD(const IoHandle&, ioHandle, (), (const, override));
  MOCK_METHOD(Socket::Type, socketType, (), (const, override));
  MOCK_METHOD(Address::Type, addressType, (), (const, override));
  MOCK_METHOD(std::optional<Address::IpVersion>, ipVersion, (), (const, override));
  MOCK_METHOD(void, setAbortiveClose, (), (override));
  MOCK_METHOD(void, close, (), (override));
  MOCK_METHOD(bool, isOpen, (), (const, override));
  MOCK_METHOD(IoHandlePtr, socket, (Socket::Type, Address::Type, Address::IpVersion), (const));
  MOCK_METHOD(IoHandlePtr, socketForAddrPtr, (Socket::Type, const Address::InstanceConstSharedPtr),
              (const));
  MOCK_METHOD(Api::SysCallIntResult, bind, (const Address::InstanceConstSharedPtr), (override));
  MOCK_METHOD(Api::SysCallIntResult, connect, (const Address::InstanceConstSharedPtr), (override));
  MOCK_METHOD(Api::SysCallIntResult, listen, (int), (override));
  MOCK_METHOD(Api::SysCallIntResult, setSocketOption, (int, int, const void*, socklen_t),
              (override));
  MOCK_METHOD(Api::SysCallIntResult, getSocketOption, (int, int, void*, socklen_t*),
              (const, override));
  MOCK_METHOD(Api::SysCallIntResult, ioctl,
              (unsigned long, void*, unsigned long, void*, unsigned long, unsigned long*),
              (override));
  MOCK_METHOD(Api::SysCallIntResult, setBlockingForTest, (bool), (override));
  MOCK_METHOD(std::optional<std::chrono::milliseconds>, lastRoundTripTime, (), (override));
  MOCK_METHOD(std::optional<uint64_t>, congestionWindowInBytes, (), (const, override));
  MOCK_METHOD(void, dumpState, (std::ostream&, int), (const, override));

  IoHandlePtr io_handle_;
  std::shared_ptr<Network::ConnectionInfoSetterImpl> connection_info_provider_;
};

class MockReadFilterCallbacks : public ReadFilterCallbacks {
public:
  MockReadFilterCallbacks();
  ~MockReadFilterCallbacks() override;

  MOCK_METHOD(Connection&, connection, (), (override));
  MOCK_METHOD(const ConnectionSocket&, socket, (), (override));
  MOCK_METHOD(void, continueReading, (), (override));
  MOCK_METHOD(void, injectReadDataToFilterChain, (Buffer::Instance & data, bool end_stream),
              (override));
  MOCK_METHOD(OptRef<Buffer::Instance>, readBuffer, (), (override));
  MOCK_METHOD(Upstream::HostDescriptionConstSharedPtr, upstreamHost, (), (override));
  MOCK_METHOD(void, upstreamHost, (Upstream::HostDescriptionConstSharedPtr host), (override));
  MOCK_METHOD(bool, startUpstreamSecureTransport, (), (override));
  MOCK_METHOD(void, disableClose, (bool disable), (override));

  testing::NiceMock<MockConnection> connection_;
  testing::NiceMock<MockConnectionSocket> socket_;
  Upstream::HostDescriptionConstSharedPtr host_;
};

class MockReadFilter : public ReadFilter {
public:
  MockReadFilter();
  ~MockReadFilter() override;

  MOCK_METHOD(FilterStatus, onData, (Buffer::Instance & data, bool end_stream), (override));
  MOCK_METHOD(FilterStatus, onNewConnection, (), (override));
  MOCK_METHOD(void, initializeReadFilterCallbacks, (ReadFilterCallbacks & callbacks), (override));
  MOCK_METHOD(bool, startUpstreamSecureTransport, (), (override));

  ReadFilterCallbacks* callbacks_{};
};

class MockWriteFilterCallbacks : public WriteFilterCallbacks {
public:
  MockWriteFilterCallbacks();
  ~MockWriteFilterCallbacks() override;

  MOCK_METHOD(Connection&, connection, (), (override));
  MOCK_METHOD(const ConnectionSocket&, socket, (), (override));
  MOCK_METHOD(void, injectWriteDataToFilterChain, (Buffer::Instance & data, bool end_stream),
              (override));
  MOCK_METHOD(void, disableClose, (bool disable), (override));
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, (), (override));
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, (), (override));

  testing::NiceMock<MockConnection> connection_;
  testing::NiceMock<MockConnectionSocket> socket_;
};

class MockWriteFilter : public WriteFilter {
public:
  MockWriteFilter();
  ~MockWriteFilter() override;

  MOCK_METHOD(FilterStatus, onWrite, (Buffer::Instance & data, bool end_stream), (override));
  MOCK_METHOD(void, initializeWriteFilterCallbacks, (WriteFilterCallbacks & callbacks), (override));

  WriteFilterCallbacks* write_callbacks_{};
};

class MockFilter : public Filter {
public:
  MockFilter();
  ~MockFilter() override;

  MOCK_METHOD(FilterStatus, onData, (Buffer::Instance & data, bool end_stream), (override));
  MOCK_METHOD(FilterStatus, onNewConnection, (), (override));
  MOCK_METHOD(FilterStatus, onWrite, (Buffer::Instance & data, bool end_stream), (override));
  MOCK_METHOD(void, initializeReadFilterCallbacks, (ReadFilterCallbacks & callbacks), (override));
  MOCK_METHOD(void, initializeWriteFilterCallbacks, (WriteFilterCallbacks & callbacks), (override));

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

  MOCK_METHOD(void, onData, (UdpRecvData && data), (override));
  MOCK_METHOD(void, onDatagramsDropped, (uint32_t dropped), (override));
  MOCK_METHOD(void, onReadReady, (), (override));
  MOCK_METHOD(void, onWriteReady, (const Socket& socket), (override));
  MOCK_METHOD(void, onReceiveError, (Api::IoError::IoErrorCode err), (override));
  MOCK_METHOD(Network::UdpPacketWriter&, udpPacketWriter, (), (override));
  MOCK_METHOD(uint32_t, workerIndex, (), (const, override));
  MOCK_METHOD(void, onDataWorker, (Network::UdpRecvData && data), (override));
  MOCK_METHOD(void, post, (Network::UdpRecvData && data), (override));
  MOCK_METHOD(size_t, numPacketsExpectedPerEventLoop, (), (const, override));
  MOCK_METHOD(const IoHandle::UdpSaveCmsgConfig&, udpSaveCmsgConfig, (), (const, override));
};

class MockDrainDecision : public DrainDecision {
public:
  MockDrainDecision();
  ~MockDrainDecision() override;

  MOCK_METHOD(bool, drainClose, (Network::DrainDirection direction), (const, override));
  MOCK_METHOD(Common::CallbackHandlePtr, addOnDrainCloseCb,
              (Network::DrainDirection direction, DrainCloseCb cb), (const, override));
};

class MockListenerFilter : public ListenerFilter {
public:
  MockListenerFilter(size_t max_read_bytes = 0) : listener_filter_max_read_bytes_(max_read_bytes) {}
  ~MockListenerFilter() override;

  size_t maxReadBytes() const override { return listener_filter_max_read_bytes_; }

  MOCK_METHOD(void, destroy_, ());
  MOCK_METHOD(Network::FilterStatus, onAccept, (ListenerFilterCallbacks&), (override));
  MOCK_METHOD(Network::FilterStatus, onData, (Network::ListenerFilterBuffer&), (override));
  MOCK_METHOD(void, onClose, (), (override));

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
  MOCK_METHOD(Network::FilterStatus, onAccept, (ListenerFilterCallbacks&), (override));
  MOCK_METHOD(bool, shouldAdvertiseServerPreferredAddress, (const quic::QuicSocketAddress&),
              (const));
  MOCK_METHOD(Network::FilterStatus, onPeerAddressChanged,
              (const quic::QuicSocketAddress&, Network::Connection&), (override));
};

class MockQuicListenerFilterManager : public QuicListenerFilterManager {
public:
  MOCK_METHOD(void, addFilter,
              (const Network::ListenerFilterMatcherSharedPtr&, QuicListenerFilterPtr&&),
              (override));
  MOCK_METHOD(bool, shouldAdvertiseServerPreferredAddress, (const quic::QuicSocketAddress&),
              (const, override));

  MOCK_METHOD(void, onPeerAddressChanged, (const quic::QuicSocketAddress&, Connection&),
              (override));
  MOCK_METHOD(void, onFirstPacketReceived, (const quic::QuicReceivedPacket&), (override));
};

#endif

class MockFilterChain : public DrainableFilterChain {
public:
  MockFilterChain();
  ~MockFilterChain() override;

  // Network::DrainableFilterChain
  MOCK_METHOD(const DownstreamTransportSocketFactory&, transportSocketFactory, (),
              (const, override));
  MOCK_METHOD(std::chrono::milliseconds, transportSocketConnectTimeout, (), (const, override));
  MOCK_METHOD(const NetworkFilterFactoriesList&, networkFilterFactories, (), (const, override));
  MOCK_METHOD(void, startDraining, (), (override));
  MOCK_METHOD(absl::string_view, name, (), (const, override));
  MOCK_METHOD(bool, addedViaApi, (), (const, override));
  MOCK_METHOD(const FilterChainInfoSharedPtr&, filterChainInfo, (), (const, override));

  envoy::config::core::v3::Metadata metadata_;
  FilterChainInfoSharedPtr filter_chain_info_;
};

class MockFilterChainInfo : public FilterChainInfo {
public:
  MockFilterChainInfo();

  // Network::FilterChainInfo
  MOCK_METHOD(absl::string_view, name, (), (const, override));
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, metadata, (), (const, override));
  MOCK_METHOD(const Envoy::Config::TypedMetadata&, typedMetadata, (), (const, override));

  std::string filter_chain_name_{"mock"};
  envoy::config::core::v3::Metadata metadata_;
};

class MockFilterChainManager : public FilterChainManager {
public:
  MockFilterChainManager();
  ~MockFilterChainManager() override;

  // Network::FilterChainManager
  MOCK_METHOD(const FilterChain*, findFilterChain,
              (const ConnectionSocket& socket, const StreamInfo::StreamInfo& info),
              (const, override));
};

class MockFilterChainFactory : public FilterChainFactory {
public:
  MockFilterChainFactory();
  ~MockFilterChainFactory() override;

  MOCK_METHOD(bool, createNetworkFilterChain,
              (Connection & connection, const NetworkFilterFactoriesList& filter_factories),
              (override));
  MOCK_METHOD(bool, createListenerFilterChain, (ListenerFilterManager & listener), (override));
  MOCK_METHOD(void, createUdpListenerFilterChain,
              (UdpListenerFilterManager & listener, UdpReadFilterCallbacks& callbacks), (override));
  MOCK_METHOD(bool, createQuicListenerFilterChain, (QuicListenerFilterManager & listener),
              (override));
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
  MOCK_METHOD(IoHandle&, ioHandle, (), (override));
  MOCK_METHOD(SocketPtr, duplicate, (), (override));
  MOCK_METHOD(const IoHandle&, ioHandle, (), (const, override));
  MOCK_METHOD(Socket::Type, socketType, (), (const, override));
  MOCK_METHOD(Address::Type, addressType, (), (const, override));
  MOCK_METHOD(std::optional<Address::IpVersion>, ipVersion, (), (const, override));
  MOCK_METHOD(void, setAbortiveClose, (), (override));
  MOCK_METHOD(void, close, (), (override));
  MOCK_METHOD(bool, isOpen, (), (const, override));
  MOCK_METHOD(void, addOption_, (const Socket::OptionConstSharedPtr& option));
  MOCK_METHOD(void, addOptions_, (const Socket::OptionsSharedPtr& options));
  MOCK_METHOD(const OptionsSharedPtr&, options, (), (const, override));
  MOCK_METHOD(IoHandlePtr, socket, (Socket::Type, Address::Type, Address::IpVersion), (const));
  MOCK_METHOD(IoHandlePtr, socketForAddrPtr, (Socket::Type, const Address::InstanceConstSharedPtr),
              (const));
  MOCK_METHOD(Api::SysCallIntResult, bind, (const Address::InstanceConstSharedPtr), (override));
  MOCK_METHOD(Api::SysCallIntResult, connect, (const Address::InstanceConstSharedPtr), (override));
  MOCK_METHOD(Api::SysCallIntResult, listen, (int), (override));
  MOCK_METHOD(Api::SysCallIntResult, setSocketOption, (int, int, const void*, socklen_t),
              (override));
  MOCK_METHOD(Api::SysCallIntResult, getSocketOption, (int, int, void*, socklen_t*),
              (const, override));
  MOCK_METHOD(Api::SysCallIntResult, ioctl,
              (unsigned long, void*, unsigned long, void*, unsigned long, unsigned long*),
              (override));
  MOCK_METHOD(Api::SysCallIntResult, setBlockingForTest, (bool), (override));

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
              (const, override));
  MOCK_METHOD(void, hashKey, (std::vector<uint8_t>&), (const, override));
  MOCK_METHOD(std::optional<Socket::Option::Details>, getOptionDetails,
              (const Socket&, envoy::config::core::v3::SocketOption::SocketState state),
              (const, override));
  MOCK_METHOD(bool, isSupported, (), (const, override));
};

class MockListenerFilterCallbacks : public ListenerFilterCallbacks {
public:
  MockListenerFilterCallbacks();
  ~MockListenerFilterCallbacks() override;

  MOCK_METHOD(ConnectionSocket&, socket, (), (override));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (override));
  MOCK_METHOD(void, continueFilterChain, (bool), (override));
  MOCK_METHOD(void, setDynamicMetadata, (const std::string&, const Protobuf::Struct&), (override));
  MOCK_METHOD(void, setDynamicTypedMetadata, (const std::string&, const Protobuf::Any& value),
              (override));
  MOCK_METHOD(envoy::config::core::v3::Metadata&, dynamicMetadata, (), (override));
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, dynamicMetadata, (), (const, override));
  MOCK_METHOD(StreamInfo::FilterState&, filterState, (), (override));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, (), (override));
  MOCK_METHOD(void, useOriginalDst, (bool), (override));

  StreamInfo::FilterStateImpl filter_state_;
  NiceMock<MockConnectionSocket> socket_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

class MockListenSocketFactory : public ListenSocketFactory {
public:
  MockListenSocketFactory() = default;

  MOCK_METHOD(Network::Socket::Type, socketType, (), (const, override));
  MOCK_METHOD(const Network::Address::InstanceConstSharedPtr&, localAddress, (), (const, override));
  MOCK_METHOD(Network::SocketSharedPtr, getListenSocket, (uint32_t), (override));
  MOCK_METHOD(bool, reusePort, (), (const));
  MOCK_METHOD(Network::ListenSocketFactoryPtr, clone, (uint32_t), (const, override));
  MOCK_METHOD(void, closeAllSockets, (), (override));
  MOCK_METHOD(absl::Status, doFinalPreWorkerInit, (), (override));
};

class MockUdpPacketWriterFactory : public UdpPacketWriterFactory {
public:
  MockUdpPacketWriterFactory() = default;

  MOCK_METHOD(Network::UdpPacketWriterPtr, createUdpPacketWriter,
              (Network::IoHandle&, Stats::Scope&, Envoy::Event::Dispatcher&,
               absl::AnyInvocable<void() &&>),
              (override));
};

class MockUdpListenerConfig : public UdpListenerConfig {
public:
  MockUdpListenerConfig(uint32_t concurrency = 1);
  ~MockUdpListenerConfig() override;

  MOCK_METHOD(ActiveUdpListenerFactory&, listenerFactory, (), (override));
  MOCK_METHOD(UdpPacketWriterFactory&, packetWriterFactory, (), (override));
  MOCK_METHOD(Envoy::Quic::QuicPacketWriterFactory*, quicPacketWriterFactory, (), (override));
  MOCK_METHOD(UdpListenerWorkerRouter&, listenerWorkerRouter, (const Network::Address::Instance&),
              (override));
  MOCK_METHOD(const envoy::config::listener::v3::UdpListenerConfig&, config, (), (override));

  UdpListenerWorkerRouterPtr udp_listener_worker_router_;
  envoy::config::listener::v3::UdpListenerConfig config_;
};

class MockListenerInfo : public ListenerInfo {
public:
  MOCK_METHOD(absl::string_view, name, (), (const, override));
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, metadata, (), (const, override));
  MOCK_METHOD(const Envoy::Config::TypedMetadata&, typedMetadata, (), (const, override));
  MOCK_METHOD(envoy::config::core::v3::TrafficDirection, direction, (), (const, override));
  MOCK_METHOD(bool, isQuic, (), (const, override));
  MOCK_METHOD(bool, shouldBypassOverloadManager, (), (const, override));
  MOCK_METHOD(envoy::config::listener::v3::Listener::DrainType, drainType, (), (const, override));
};

class MockListenerConfig : public ListenerConfig {
public:
  MockListenerConfig();
  ~MockListenerConfig() override;

  MOCK_METHOD(FilterChainManager&, filterChainManager, (), (override));
  MOCK_METHOD(FilterChainFactory&, filterChainFactory, (), (override));
  MOCK_METHOD(std::vector<ListenSocketFactoryPtr>&, listenSocketFactories, (), (override));
  MOCK_METHOD(bool, bindToPort, (), (const, override));
  MOCK_METHOD(bool, handOffRestoredDestinationConnections, (), (const, override));
  MOCK_METHOD(uint32_t, perConnectionBufferLimitBytes, (), (const, override));
  MOCK_METHOD(std::chrono::milliseconds, perConnectionBufferHighWatermarkTimeout, (),
              (const, override));
  MOCK_METHOD(std::chrono::milliseconds, listenerFiltersTimeout, (), (const, override));
  MOCK_METHOD(bool, continueOnListenerFiltersTimeout, (), (const, override));
  MOCK_METHOD(Stats::Scope&, listenerScope, (), (override));
  MOCK_METHOD(uint64_t, listenerTag, (), (const, override));
  MOCK_METHOD(const std::string&, name, (), (const, override));
  MOCK_METHOD(Network::UdpListenerConfigOptRef, udpListenerConfig, (), (override));
  MOCK_METHOD(InternalListenerConfigOptRef, internalListenerConfig, (), (override));
  MOCK_METHOD(ConnectionBalancer&, connectionBalancer, (const Network::Address::Instance&),
              (override));
  MOCK_METHOD(ResourceLimit&, openConnections, (), (override));
  MOCK_METHOD(uint32_t, tcpBacklogSize, (), (const, override));
  MOCK_METHOD(uint32_t, maxConnectionsToAcceptPerSocketEvent, (), (const, override));
  MOCK_METHOD(Init::Manager&, initManager, (), (override));
  MOCK_METHOD(bool, ignoreGlobalConnLimit, (), (const, override));
  MOCK_METHOD(bool, shouldBypassOverloadManager, (), (const, override));

  const AccessLog::InstanceSharedPtrVector& accessLogs() const override {
    return empty_access_logs_;
  }

  const ListenerInfoConstSharedPtr& listenerInfo() const override { return listener_info_; }

  testing::NiceMock<MockFilterChainFactory> filter_chain_factory_;
  std::vector<ListenSocketFactoryPtr> socket_factories_;
  SocketSharedPtr socket_;
  ListenerInfoConstSharedPtr listener_info_;
  Stats::IsolatedStoreImpl store_;
  std::string name_;
  const AccessLog::InstanceSharedPtrVector empty_access_logs_;
};

class MockListener : public Listener {
public:
  MockListener();
  ~MockListener() override;

  MOCK_METHOD(void, onDestroy, ());
  MOCK_METHOD(void, enable, (), (override));
  MOCK_METHOD(void, disable, (), (override));
  MOCK_METHOD(void, setRejectFraction, (UnitFloat), (override));
  MOCK_METHOD(void, configureLoadShedPoints, (Server::LoadShedPointProvider&), (override));
  MOCK_METHOD(bool, shouldBypassOverloadManager, (), (const, override));
};

class MockConnectionHandler : public virtual ConnectionHandler {
public:
  MockConnectionHandler();
  ~MockConnectionHandler() override;

  MOCK_METHOD(uint64_t, numConnections, (), (const, override));
  MOCK_METHOD(void, incNumConnections, (), (override));
  MOCK_METHOD(void, decNumConnections, (), (override));
  MOCK_METHOD(void, addListener,
              (std::optional<uint64_t> overridden_listener, ListenerConfig& config,
               Runtime::Loader& runtime, Random::RandomGenerator& random),
              (override));
  MOCK_METHOD(void, removeListeners, (uint64_t listener_tag), (override));
  MOCK_METHOD(void, removeFilterChains,
              (uint64_t listener_tag, const std::list<const Network::FilterChain*>& filter_chains,
               std::function<void()> completion),
              (override));
  MOCK_METHOD(void, stopListeners,
              (uint64_t listener_tag, const Network::ExtraShutdownListenerOptions& options),
              (override));
  MOCK_METHOD(void, stopListeners, (), (override));
  MOCK_METHOD(void, onFilterChainDrain,
              (uint64_t listener_tag, const std::list<const Network::FilterChain*>& filter_chains,
               Network::ConnectionDrainEvent drain_event),
              (override));
  MOCK_METHOD(void, onListenerDrain,
              (uint64_t listener_tag, Network::ConnectionDrainEvent drain_event), (override));
  MOCK_METHOD(void, disableListeners, (), (override));
  MOCK_METHOD(void, enableListeners, (), (override));
  MOCK_METHOD(void, setListenerRejectFraction, (UnitFloat), (override));
  MOCK_METHOD(const std::string&, statPrefix, (), (const, override));
  MOCK_METHOD(std::optional<uint32_t>, workerId, (), (const, override));
  MOCK_METHOD(void, closeIdleHttpConnections, (bool), (override));

  uint64_t num_handler_connections_{};
};

class MockUdpListenerWorkerRouter : public UdpListenerWorkerRouter {
public:
  ~MockUdpListenerWorkerRouter() override;

  MOCK_METHOD(void, registerWorkerForListener, (UdpListenerCallbacks & listener), (override));
  MOCK_METHOD(void, unregisterWorkerForListener, (UdpListenerCallbacks & listener), (override));
  MOCK_METHOD(void, deliver, (uint32_t dest_worker_index, UdpRecvData&& data), (override));
};

class MockNonDispatchedUdpPacketHandler : public NonDispatchedUdpPacketHandler {
public:
  ~MockNonDispatchedUdpPacketHandler() override;

  MOCK_METHOD(void, handle,
              (uint32_t worker_index, const Address::Instance& listener_address,
               const UdpRecvData& packet),
              (override));
};

class MockIp : public Address::Ip {
public:
  MockIp();
  ~MockIp() override;

  MOCK_METHOD(const std::string&, addressAsString, (), (const, override));
  MOCK_METHOD(bool, isAnyAddress, (), (const, override));
  MOCK_METHOD(bool, isUnicastAddress, (), (const, override));
  MOCK_METHOD(bool, isLinkLocalAddress, (), (const, override));
  MOCK_METHOD(bool, isUniqueLocalAddress, (), (const, override));
  MOCK_METHOD(bool, isSiteLocalAddress, (), (const, override));
  MOCK_METHOD(bool, isTeredoAddress, (), (const, override));
  MOCK_METHOD(const Address::Ipv4*, ipv4, (), (const, override));
  MOCK_METHOD(const Address::Ipv6*, ipv6, (), (const, override));
  MOCK_METHOD(uint32_t, port, (), (const, override));
  MOCK_METHOD(Address::IpVersion, version, (), (const, override));
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
  MOCK_METHOD(const Address::Ip*, ip, (), (const, override));
  MOCK_METHOD(const Address::Pipe*, pipe, (), (const, override));
  MOCK_METHOD(Address::EnvoyInternalAddress*, envoyInternalAddress, (), (const, override));
  MOCK_METHOD(IoHandlePtr, socket, (Socket::Type), (const));
  MOCK_METHOD(Address::Type, type, (), (const, override));
  MOCK_METHOD(const sockaddr*, sockAddr, (), (const, override));
  MOCK_METHOD(socklen_t, sockAddrLen, (), (const, override));
  MOCK_METHOD(absl::string_view, addressType, (), (const, override));
  MOCK_METHOD(std::optional<std::string>, networkNamespace, (), (const, override));
  MOCK_METHOD(Address::InstanceConstSharedPtr, withNetworkNamespace, (absl::string_view),
              (const, override));

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

  MOCK_METHOD(IoHandle&, ioHandle, (), (override));
  MOCK_METHOD(const IoHandle&, ioHandle, (), (const, override));
  MOCK_METHOD(Connection&, connection, (), (override));
  MOCK_METHOD(bool, shouldDrainReadBuffer, (), (override));
  MOCK_METHOD(void, setTransportSocketIsReadable, (), (override));
  MOCK_METHOD(void, raiseEvent, (ConnectionEvent), (override));
  MOCK_METHOD(void, flushWriteBuffer, (), (override));

  testing::NiceMock<MockConnection> connection_;
};

class MockUdpPacketWriter : public UdpPacketWriter {
public:
  MockUdpPacketWriter();
  ~MockUdpPacketWriter() override;

  MOCK_METHOD(Api::IoCallUint64Result, writePacket,
              (const Buffer::Instance& buffer, const Address::Ip* local_ip,
               const Address::Instance& peer_address),
              (override));
  MOCK_METHOD(bool, isWriteBlocked, (), (const, override));
  MOCK_METHOD(void, setWritable, (), (override));
  MOCK_METHOD(uint64_t, getMaxPacketSize, (const Address::Instance& peer_address),
              (const, override));
  MOCK_METHOD(bool, isBatchMode, (), (const, override));
  MOCK_METHOD(Network::UdpPacketWriterBuffer, getNextWriteLocation,
              (const Address::Ip* local_ip, const Address::Instance& peer_address), (override));
  MOCK_METHOD(Api::IoCallUint64Result, flush, (), (override));
};

class MockUdpListener : public UdpListener {
public:
  MockUdpListener();
  ~MockUdpListener() override;

  MOCK_METHOD(void, onDestroy, ());
  MOCK_METHOD(void, enable, (), (override));
  MOCK_METHOD(void, disable, (), (override));
  MOCK_METHOD(void, setRejectFraction, (UnitFloat), (override));
  MOCK_METHOD(void, configureLoadShedPoints, (Server::LoadShedPointProvider&), (override));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (override));
  MOCK_METHOD(Address::InstanceConstSharedPtr&, localAddress, (), (const, override));
  MOCK_METHOD(Api::IoCallUint64Result, send, (const UdpSendData&), (override));
  MOCK_METHOD(Api::IoCallUint64Result, flush, (), (override));
  MOCK_METHOD(void, activateRead, (), (override));
  MOCK_METHOD(bool, shouldBypassOverloadManager, (), (const, override));

  Event::MockDispatcher dispatcher_;
};

class MockUdpReadFilterCallbacks : public UdpReadFilterCallbacks {
public:
  MockUdpReadFilterCallbacks();
  ~MockUdpReadFilterCallbacks() override;

  MOCK_METHOD(UdpListener&, udpListener, (), (override));
  MOCK_METHOD(UdpHotRestartSessionHandlePtr, registerHotRestartSession,
              (const Address::InstanceConstSharedPtr& local_address,
               const Address::InstanceConstSharedPtr& peer_address),
              (override));

  testing::NiceMock<MockUdpListener> udp_listener_;
};

class MockUdpListenerReadFilter : public UdpListenerReadFilter {
public:
  MockUdpListenerReadFilter(UdpReadFilterCallbacks& callbacks);
  ~MockUdpListenerReadFilter() override;

  MOCK_METHOD(Network::FilterStatus, onData, (UdpRecvData&), (override));
  MOCK_METHOD(Network::FilterStatus, onReceiveError, (Api::IoError::IoErrorCode), (override));
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

  MOCK_METHOD(void, registerHandler, (BalancedConnectionHandler & handler), (override));
  MOCK_METHOD(void, unregisterHandler, (BalancedConnectionHandler & handler), (override));
  MOCK_METHOD(BalancedConnectionHandler&, pickTargetHandler,
              (BalancedConnectionHandler & current_handler), (override));
};

class MockListenerFilterMatcher : public ListenerFilterMatcher {
public:
  MockListenerFilterMatcher();
  ~MockListenerFilterMatcher() override;
  MOCK_METHOD(bool, matches, (Network::ListenerFilterCallbacks & cb), (const, override));
};

class MockUdpPacketProcessor : public UdpPacketProcessor {
public:
  MOCK_METHOD(void, processPacket,
              (Address::InstanceConstSharedPtr local_address,
               Address::InstanceConstSharedPtr peer_address, Buffer::InstancePtr buffer,
               MonotonicTime receive_time, uint8_t tos, Buffer::OwnedImpl saved_cmsg),
              (override));
  MOCK_METHOD(void, onDatagramsDropped, (uint32_t dropped), (override));
  MOCK_METHOD(uint64_t, maxDatagramSize, (), (const, override));
  MOCK_METHOD(size_t, numPacketsExpectedPerEventLoop, (), (const, override));
  MOCK_METHOD(const IoHandle::UdpSaveCmsgConfig&, saveCmsgConfig, (), (const, override));
};

class MockSocketInterface : public SocketInterfaceImpl {
public:
  explicit MockSocketInterface(const std::vector<Address::IpVersion>& versions)
      : versions_(versions.begin(), versions.end()) {}
  MOCK_METHOD(IoHandlePtr, socket,
              (Socket::Type, Address::Type, Address::IpVersion, bool, const SocketCreationOptions&),
              (const, override));
  MOCK_METHOD(IoHandlePtr, socket,
              (Socket::Type, const Address::InstanceConstSharedPtr, const SocketCreationOptions&),
              (const, override));
  bool ipFamilySupported(int domain) override {
    const auto to_version = domain == AF_INET ? Address::IpVersion::v4 : Address::IpVersion::v6;
    return std::any_of(versions_.begin(), versions_.end(),
                       [to_version](auto version) { return to_version == version; });
  }
  const std::vector<Address::IpVersion> versions_;
};

class MockConnectionInfoProvider : public ConnectionInfoProvider {
public:
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, localAddress, (), (const, override));
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, directLocalAddress, (), (const, override));
  MOCK_METHOD(bool, localAddressRestored, (), (const, override));
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, remoteAddress, (), (const, override));
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, directRemoteAddress, (), (const, override));
  MOCK_METHOD(absl::string_view, requestedServerName, (), (const, override));
  MOCK_METHOD(const std::vector<std::string>&, requestedApplicationProtocols, (),
              (const, override));
  MOCK_METHOD(std::optional<uint64_t>, connectionID, (), (const, override));
  MOCK_METHOD(std::optional<absl::string_view>, interfaceName, (), (const, override));
  MOCK_METHOD(void, dumpState, (std::ostream&, int), (const, override));
  MOCK_METHOD(Envoy::Ssl::ConnectionInfoConstSharedPtr, sslConnection, (), (const, override));
  MOCK_METHOD(absl::string_view, ja3Hash, (), (const, override));
  MOCK_METHOD(const std::optional<std::chrono::milliseconds>&, roundTripTime, (),
              (const, override));
  MOCK_METHOD(Envoy::OptRef<const Envoy::Network::FilterChainInfo>, filterChainInfo, (),
              (const, override));
  MOCK_METHOD(Envoy::OptRef<const Envoy::Network::ListenerInfo>, listenerInfo, (),
              (const, override));
  MOCK_METHOD(absl::string_view, ja4Hash, (), (const, override));
};

} // namespace Network
} // namespace Envoy
