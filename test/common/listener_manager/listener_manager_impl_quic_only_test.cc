#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"

#if defined(ENVOY_ENABLE_QUIC)
#include "source/common/quic/quic_server_transport_socket_factory.h"
#endif

#include "test/common/listener_manager/listener_manager_impl_test.h"
#include "test/server/utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/mocks/network/mocks.h"
#include "test/integration/filters/test_listener_filter.h"

namespace Envoy {
namespace Server {
namespace {

using ::testing::Return;

class MockSupportsUdpGso : public Api::OsSysCallsImpl {
public:
  MOCK_METHOD(bool, supportsUdpGso, (), (const));
};

class ListenerManagerImplQuicOnlyTest : public ListenerManagerImplTest {
public:
  size_t expectedNumSocketOptions() {
    // SO_REUSEPORT, IP_PKTINFO and IP_MTU_DISCOVER/IP_DONTFRAG.
    const size_t num_platform_independent_socket_options =
        Runtime::runtimeFeatureEnabled("envoy.reloadable_features.udp_set_do_not_fragment") ? 3 : 2;
    size_t num_platform_dependent_socket_options = 0;
#ifdef SO_RXQ_OVFL
    ++num_platform_dependent_socket_options;
#endif
    if (Api::OsSysCallsSingleton::get().supportsUdpGro()) {
      // SO_REUSEPORT
      ++num_platform_dependent_socket_options;
    }
    return num_platform_dependent_socket_options + num_platform_independent_socket_options;
  }

  NiceMock<MockSupportsUdpGso> udp_gso_syscall_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&udp_gso_syscall_};
  Api::OsSysCallsImpl os_sys_calls_actual_;
};

#if defined(ENVOY_ENABLE_QUIC)
std::string getBasicConfig() {
  std::string yaml = TestEnvironment::substitute(R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    protocol: UDP
    port_value: 1234
filter_chains:
- filter_chain_match:
    transport_protocol: "quic"
  name: foo
  filters:
  - name: envoy.filters.network.http_connection_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
      codec_type: HTTP3
      stat_prefix: hcm
      route_config:
        name: local_route
      http_filters:
        - name: envoy.filters.http.router
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  transport_socket:
    name: envoy.transport_sockets.quic
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicDownstreamTransport
      downstream_tls_context:
        common_tls_context:
          tls_certificates:
          - certificate_chain:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
            private_key:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
          validation_context:
            trusted_ca:
              filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
            match_typed_subject_alt_names:
            - matcher:
                exact: localhost
              san_type: URI
            - matcher:
                exact: 127.0.0.1
              san_type: IP_ADDRESS
udp_listener_config:
  quic_options: {}
  )EOF",
                                                 Network::Address::IpVersion::v4);
  return yaml;
}
TEST_P(ListenerManagerImplQuicOnlyTest, QuicListenerFactoryAndSslContext) {
  std::string yaml = getBasicConfig();
  if (use_matcher_) {
    yaml = yaml + R"EOF(
filter_chain_matcher:
  matcher_tree:
    input:
      name: transport
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.TransportProtocolInput
    exact_match_map:
      map:
        "quic":
          action:
            name: foo
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
    )EOF";
  }

  envoy::config::listener::v3::Listener listener_proto = parseListenerFromV3Yaml(yaml);
  ON_CALL(udp_gso_syscall_, supportsUdpGso())
      .WillByDefault(Return(os_sys_calls_actual_.supportsUdpGso()));
  EXPECT_CALL(server_.api_.random_, uuid());
  expectCreateListenSocket(envoy::config::core::v3::SocketOption::STATE_PREBIND,
                           expectedNumSocketOptions(),
                           ListenerComponentFactory::BindType::ReusePort);

  expectSetsockopt(/* expected_sockopt_level */ IPPROTO_IP,
                   /* expected_sockopt_name */ ENVOY_IP_PKTINFO,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#ifdef SO_RXQ_OVFL
  expectSetsockopt(/* expected_sockopt_level */ SOL_SOCKET,
                   /* expected_sockopt_name */ SO_RXQ_OVFL,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#endif
  expectSetsockopt(/* expected_sockopt_level */ SOL_SOCKET,
                   /* expected_sockopt_name */ SO_REUSEPORT,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#ifdef UDP_GRO
  if (Api::OsSysCallsSingleton::get().supportsUdpGro()) {
    expectSetsockopt(/* expected_sockopt_level */ SOL_UDP,
                     /* expected_sockopt_name */ UDP_GRO,
                     /* expected_value */ 1,
                     /* expected_num_calls */ 1);
  }
#endif

#ifdef ENVOY_IP_DONTFRAG
  expectSetsockopt(/* expected_sockopt_level */ IPPROTO_IP,
                   /* expected_sockopt_name */ IP_DONTFRAG,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#else
  expectSetsockopt(/* expected_sockopt_level */ IPPROTO_IP,
                   /* expected_sockopt_name */ IP_MTU_DISCOVER,
                   /* expected_value */ IP_PMTUDISC_DO,
                   /* expected_num_calls */ 1);
#endif

  addOrUpdateListener(listener_proto);
  EXPECT_EQ(1u, manager_->listeners().size());
  EXPECT_FALSE(manager_->listeners()[0]
                   .get()
                   .udpListenerConfig()
                   ->listenerFactory()
                   .isTransportConnectionless());
  Network::SocketSharedPtr listen_socket =
      manager_->listeners().front().get().listenSocketFactories()[0]->getListenSocket(0);

  Network::UdpPacketWriterPtr udp_packet_writer =
      manager_->listeners()
          .front()
          .get()
          .udpListenerConfig()
          ->packetWriterFactory()
          .createUdpPacketWriter(listen_socket->ioHandle(),
                                 manager_->listeners()[0].get().listenerScope());
  EXPECT_EQ(udp_packet_writer->isBatchMode(), Api::OsSysCallsSingleton::get().supportsUdpGso());

  // No filter chain found with non-matching transport protocol.
  EXPECT_EQ(nullptr, findFilterChain(1234, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111));

  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "quic", {}, "8.8.8.8", 111);
  ASSERT_NE(nullptr, filter_chain);
  auto& quic_socket_factory = dynamic_cast<const Quic::QuicServerTransportSocketFactory&>(
      filter_chain->transportSocketFactory());
  EXPECT_TRUE(quic_socket_factory.implementsSecureTransport());
  auto [cert, key] = quic_socket_factory.getTlsCertificateAndKey("", nullptr);
  EXPECT_TRUE(cert != nullptr);
  EXPECT_TRUE(key != nullptr);
  EXPECT_TRUE(listener_factory_.socket_->socket_is_open_);

  // Stop listening shouldn't close the socket.
  EXPECT_CALL(server_.dispatcher_, post(_)).WillOnce([](Event::PostCb callback) { callback(); });
  EXPECT_CALL(*worker_, stopListener(_, _, _))
      .WillOnce(Invoke([](Network::ListenerConfig&, const Network::ExtraShutdownListenerOptions&,
                          std::function<void()> completion) { completion(); }));
  manager_->stopListeners(ListenerManager::StopListenersType::All, {});
  EXPECT_CALL(*listener_factory_.socket_, close()).Times(0u);
  EXPECT_TRUE(listener_factory_.socket_->socket_is_open_);
}

TEST_P(ListenerManagerImplQuicOnlyTest, QuicWriterFromConfig) {
  std::string yaml = getBasicConfig();
  yaml = yaml + R"EOF(
  udp_packet_packet_writer_config:
    name: envoy.udp_packet_writer.default
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.udp_packet_writer.v3.UdpDefaultWriterFactory
  )EOF";

  envoy::config::listener::v3::Listener listener_proto = parseListenerFromV3Yaml(yaml);
  // Configure GSO support but later verify that the default writer is used instead.
  ON_CALL(udp_gso_syscall_, supportsUdpGso()).WillByDefault(Return(true));
  EXPECT_CALL(server_.api_.random_, uuid());
  expectCreateListenSocket(envoy::config::core::v3::SocketOption::STATE_PREBIND,
                           expectedNumSocketOptions(),
                           ListenerComponentFactory::BindType::ReusePort);

  expectSetsockopt(/* expected_sockopt_level */ IPPROTO_IP,
                   /* expected_sockopt_name */ ENVOY_IP_PKTINFO,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#ifdef SO_RXQ_OVFL
  expectSetsockopt(/* expected_sockopt_level */ SOL_SOCKET,
                   /* expected_sockopt_name */ SO_RXQ_OVFL,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#endif
  expectSetsockopt(/* expected_sockopt_level */ SOL_SOCKET,
                   /* expected_sockopt_name */ SO_REUSEPORT,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#ifdef UDP_GRO
  if (Api::OsSysCallsSingleton::get().supportsUdpGro()) {
    expectSetsockopt(/* expected_sockopt_level */ SOL_UDP,
                     /* expected_sockopt_name */ UDP_GRO,
                     /* expected_value */ 1,
                     /* expected_num_calls */ 1);
  }
#endif

#ifdef ENVOY_IP_DONTFRAG
  expectSetsockopt(/* expected_sockopt_level */ IPPROTO_IP,
                   /* expected_sockopt_name */ IP_DONTFRAG,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#else
  expectSetsockopt(/* expected_sockopt_level */ IPPROTO_IP,
                   /* expected_sockopt_name */ IP_MTU_DISCOVER,
                   /* expected_value */ IP_PMTUDISC_DO,
                   /* expected_num_calls */ 1);
#endif

  addOrUpdateListener(listener_proto);
  EXPECT_EQ(1u, manager_->listeners().size());
  EXPECT_FALSE(manager_->listeners()[0]
                   .get()
                   .udpListenerConfig()
                   ->listenerFactory()
                   .isTransportConnectionless());
  Network::SocketSharedPtr listen_socket =
      manager_->listeners().front().get().listenSocketFactories()[0]->getListenSocket(0);

  Network::UdpPacketWriterFactory& udp_packet_writer_factory =
      manager_->listeners().front().get().udpListenerConfig()->packetWriterFactory();
  Network::UdpPacketWriterPtr udp_packet_writer = udp_packet_writer_factory.createUdpPacketWriter(
      listen_socket->ioHandle(), manager_->listeners()[0].get().listenerScope());
  // Even though GSO is enabled, the default writer should be used.
  EXPECT_EQ(false, udp_packet_writer->isBatchMode());
}

TEST_P(ListenerManagerImplQuicOnlyTest, QuicListenerFactoryWithExplictConnectionIdConfig) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    protocol: UDP
    port_value: 1234
filter_chains:
- filter_chain_match:
    transport_protocol: "quic"
  name: foo
  filters:
  - name: envoy.filters.network.http_connection_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
      codec_type: HTTP3
      stat_prefix: hcm
      route_config:
        name: local_route
      http_filters:
        - name: envoy.filters.http.router
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  transport_socket:
    name: envoy.transport_sockets.quic
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicDownstreamTransport
      downstream_tls_context:
        common_tls_context:
          tls_certificates:
          - certificate_chain:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
            private_key:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
          validation_context:
            trusted_ca:
              filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
            match_typed_subject_alt_names:
            - matcher:
                exact: localhost
              san_type: URI
            - matcher:
                exact: 127.0.0.1
              san_type: IP_ADDRESS
udp_listener_config:
  quic_options:
    connection_id_generator_config:
      name: envoy.quic.deterministic_connection_id_generator
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.quic.connection_id_generator.v3.DeterministicConnectionIdGeneratorConfig
  )EOF",
                                                       Network::Address::IpVersion::v4);

  envoy::config::listener::v3::Listener listener_proto = parseListenerFromV3Yaml(yaml);
  // Configure GSO support but later verify that the default writer is used instead.
  ON_CALL(udp_gso_syscall_, supportsUdpGso()).WillByDefault(Return(true));
  EXPECT_CALL(server_.api_.random_, uuid());
  expectCreateListenSocket(envoy::config::core::v3::SocketOption::STATE_PREBIND,
                           expectedNumSocketOptions(),
                           ListenerComponentFactory::BindType::ReusePort);

  expectSetsockopt(/* expected_sockopt_level */ IPPROTO_IP,
                   /* expected_sockopt_name */ ENVOY_IP_PKTINFO,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#ifdef SO_RXQ_OVFL
  expectSetsockopt(/* expected_sockopt_level */ SOL_SOCKET,
                   /* expected_sockopt_name */ SO_RXQ_OVFL,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#endif
  expectSetsockopt(/* expected_sockopt_level */ SOL_SOCKET,
                   /* expected_sockopt_name */ SO_REUSEPORT,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#ifdef UDP_GRO
  if (Api::OsSysCallsSingleton::get().supportsUdpGro()) {
    expectSetsockopt(/* expected_sockopt_level */ SOL_UDP,
                     /* expected_sockopt_name */ UDP_GRO,
                     /* expected_value */ 1,
                     /* expected_num_calls */ 1);
  }
#endif

#ifdef ENVOY_IP_DONTFRAG
  expectSetsockopt(/* expected_sockopt_level */ IPPROTO_IP,
                   /* expected_sockopt_name */ IP_DONTFRAG,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#else
  expectSetsockopt(/* expected_sockopt_level */ IPPROTO_IP,
                   /* expected_sockopt_name */ IP_MTU_DISCOVER,
                   /* expected_value */ IP_PMTUDISC_DO,
                   /* expected_num_calls */ 1);
#endif

  addOrUpdateListener(listener_proto);
  EXPECT_EQ(1u, manager_->listeners().size());
  EXPECT_FALSE(manager_->listeners()[0]
                   .get()
                   .udpListenerConfig()
                   ->listenerFactory()
                   .isTransportConnectionless());
}

TEST_P(ListenerManagerImplQuicOnlyTest, QuicListenerFilterFromConfig) {
  std::string yaml = getBasicConfig();
  yaml = yaml + R"EOF(
listener_filters:
- name: envoy.filters.quic_listener.test
  typed_config:
    "@type": type.googleapis.com/test.integration.filters.TestQuicListenerFilterConfig
    added_value: xyz
  )EOF";

  envoy::config::listener::v3::Listener listener_proto = parseListenerFromV3Yaml(yaml);
  // Configure GSO support but later verify that the default writer is used instead.
  ON_CALL(udp_gso_syscall_, supportsUdpGso()).WillByDefault(Return(true));
  EXPECT_CALL(server_.api_.random_, uuid());
  expectCreateListenSocket(envoy::config::core::v3::SocketOption::STATE_PREBIND,
                           expectedNumSocketOptions(),
                           ListenerComponentFactory::BindType::ReusePort);

  expectSetsockopt(/* expected_sockopt_level */ IPPROTO_IP,
                   /* expected_sockopt_name */ ENVOY_IP_PKTINFO,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#ifdef SO_RXQ_OVFL
  expectSetsockopt(/* expected_sockopt_level */ SOL_SOCKET,
                   /* expected_sockopt_name */ SO_RXQ_OVFL,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#endif
  expectSetsockopt(/* expected_sockopt_level */ SOL_SOCKET,
                   /* expected_sockopt_name */ SO_REUSEPORT,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#ifdef UDP_GRO
  if (Api::OsSysCallsSingleton::get().supportsUdpGro()) {
    expectSetsockopt(/* expected_sockopt_level */ SOL_UDP,
                     /* expected_sockopt_name */ UDP_GRO,
                     /* expected_value */ 1,
                     /* expected_num_calls */ 1);
  }
#endif

#ifdef ENVOY_IP_DONTFRAG
  expectSetsockopt(/* expected_sockopt_level */ IPPROTO_IP,
                   /* expected_sockopt_name */ IP_DONTFRAG,
                   /* expected_value */ 1,
                   /* expected_num_calls */ 1);
#else
  expectSetsockopt(/* expected_sockopt_level */ IPPROTO_IP,
                   /* expected_sockopt_name */ IP_MTU_DISCOVER,
                   /* expected_value */ IP_PMTUDISC_DO,
                   /* expected_num_calls */ 1);
#endif

  addOrUpdateListener(listener_proto);
  EXPECT_EQ(1u, manager_->listeners().size());
  // Verify that the right filter chain type is installed.
  Network::MockQuicListenerFilterManager filter_manager;
  Network::QuicListenerFilterPtr listener_filter;
  EXPECT_CALL(filter_manager, addFilter(_, _))
      .WillOnce([&listener_filter](const Network::ListenerFilterMatcherSharedPtr&,
                                   Network::QuicListenerFilterPtr&& filter) {
        listener_filter = std::move(filter);
      });
  manager_->listeners()[0].get().filterChainFactory().createQuicListenerFilterChain(filter_manager);
  ASSERT_NE(nullptr, listener_filter);
  Network::MockListenerFilterCallbacks callbacks;
  EXPECT_CALL(callbacks, filterState()).Times(2);
  EXPECT_CALL(callbacks, dispatcher()).WillOnce(ReturnRef(server_.dispatcher_));
  listener_filter->onAccept(callbacks);
  auto* added_filter_state =
      callbacks.filter_state_.getDataReadOnly<TestQuicListenerFilter::TestStringFilterState>(
          TestQuicListenerFilter::TestStringFilterState::key());
  ASSERT_NE(nullptr, added_filter_state);
  EXPECT_EQ("xyz", added_filter_state->asString());
}

#endif

TEST_P(ListenerManagerImplQuicOnlyTest, QuicListenerFactoryWithWrongTransportSocket) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    protocol: UDP
    port_value: 1234
filter_chains:
- filter_chain_match:
    transport_protocol: "quic"
  name: foo
  filters: []
  transport_socket:
    name: envoy.transport_sockets.quic
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
      common_tls_context:
        tls_certificates:
        - certificate_chain:
            filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
          private_key:
            filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
        validation_context:
          trusted_ca:
            filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
          match_typed_subject_alt_names:
          - matcher:
              exact: localhost
            san_type: URI
          - matcher:
              exact: 127.0.0.1
            san_type: IP_ADDRESS
udp_listener_config:
  quic_options: {}
  )EOF",
                                                       Network::Address::IpVersion::v4);

  envoy::config::listener::v3::Listener listener_proto = parseListenerFromV3Yaml(yaml);

#if defined(ENVOY_ENABLE_QUIC)
  EXPECT_THROW_WITH_REGEX(addOrUpdateListener(listener_proto), EnvoyException,
                          "wrong transport socket config specified for quic transport socket");
#else
  EXPECT_THROW_WITH_REGEX(addOrUpdateListener(listener_proto), EnvoyException,
                          "QUIC is configured but not enabled in the build.");
#endif
}

TEST_P(ListenerManagerImplQuicOnlyTest, QuicListenerFactoryWithWrongCodec) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    protocol: UDP
    port_value: 1234
filter_chains:
- filter_chain_match:
    transport_protocol: "quic"
  name: foo
  filters: []
  transport_socket:
    name: envoy.transport_sockets.quic
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicDownstreamTransport
      downstream_tls_context:
        common_tls_context:
          tls_certificates:
          - certificate_chain:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
            private_key:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
          validation_context:
            trusted_ca:
              filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
            match_typed_subject_alt_names:
            - matcher:
                exact: localhost
              san_type: URI
            - matcher:
                exact: 127.0.0.1
              san_type: IP_ADDRESS
udp_listener_config:
  quic_options: {}
  )EOF",
                                                       Network::Address::IpVersion::v4);

  envoy::config::listener::v3::Listener listener_proto = parseListenerFromV3Yaml(yaml);

#if defined(ENVOY_ENABLE_QUIC)
  EXPECT_THROW_WITH_REGEX(addOrUpdateListener(listener_proto), EnvoyException,
                          "error building network filter chain for quic listener: requires "
                          "http_connection_manager filter to be last in the chain.");
#else
  EXPECT_THROW_WITH_REGEX(addOrUpdateListener(listener_proto), EnvoyException,
                          "QUIC is configured but not enabled in the build.");
#endif
}

TEST_P(ListenerManagerImplQuicOnlyTest, QuicListenerFactoryWithNetworkFilterAfterHcm) {
  std::string yaml = TestEnvironment::substitute(R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    protocol: UDP
    port_value: 1234
filter_chains:
- filter_chain_match:
    transport_protocol: "quic"
  name: foo
  filters:
  - name: envoy.filters.network.http_connection_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
      codec_type: HTTP3
      stat_prefix: hcm
      route_config:
        name: local_route
      http_filters:
        - name: envoy.filters.http.router
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  - name: envoy.test.test_network_filter
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.TestNetworkFilterConfig
  transport_socket:
    name: envoy.transport_sockets.quic
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicDownstreamTransport
      downstream_tls_context:
        common_tls_context:
          tls_certificates:
          - certificate_chain:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
            private_key:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
          validation_context:
            trusted_ca:
              filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
            match_typed_subject_alt_names:
            - matcher:
                exact: localhost
              san_type: URI
            - matcher:
                exact: 127.0.0.1
              san_type: IP_ADDRESS
udp_listener_config:
  quic_options: {}
  )EOF",
                                                 Network::Address::IpVersion::v4);
  envoy::config::listener::v3::Listener listener_proto = parseListenerFromV3Yaml(yaml);

#if defined(ENVOY_ENABLE_QUIC)
  EXPECT_THROW_WITH_REGEX(addOrUpdateListener(listener_proto), EnvoyException,
                          "error building network filter chain for quic listener: requires "
                          "http_connection_manager filter to be last in the chain.");
#else
  EXPECT_THROW_WITH_REGEX(addOrUpdateListener(listener_proto), EnvoyException,
                          "QUIC is configured but not enabled in the build.");
#endif
}

TEST_P(ListenerManagerImplQuicOnlyTest, QuicListenerFactoryWithConnectionBalencer) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    protocol: UDP
    port_value: 1234
filter_chains:
- filter_chain_match:
    transport_protocol: "quic"
  name: foo
  filters:
  - name: envoy.filters.network.http_connection_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
      codec_type: HTTP3
      stat_prefix: hcm
      route_config:
        name: local_route
      http_filters:
        - name: envoy.filters.http.router
  transport_socket:
    name: envoy.transport_sockets.quic
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicDownstreamTransport
      downstream_tls_context:
        common_tls_context:
          tls_certificates:
          - certificate_chain:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
            private_key:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
          validation_context:
            trusted_ca:
              filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
            match_typed_subject_alt_names:
            - matcher:
                exact: localhost
              san_type: URI
            - matcher:
                exact: 127.0.0.1
              san_type: IP_ADDRESS
udp_listener_config:
  quic_options: {}
connection_balance_config:
  exact_balance: {}
  )EOF",
                                                       Network::Address::IpVersion::v4);

  envoy::config::listener::v3::Listener listener_proto = parseListenerFromV3Yaml(yaml);

#if defined(ENVOY_ENABLE_QUIC)
  EXPECT_THROW_WITH_REGEX(addOrUpdateListener(listener_proto), EnvoyException,
                          "connection_balance_config is configured for QUIC listener which doesn't "
                          "work with connection balancer.");
#else
  EXPECT_THROW_WITH_REGEX(addOrUpdateListener(listener_proto), EnvoyException,
                          "QUIC is configured but not enabled in the build.");
#endif
}

TEST_P(ListenerManagerImplQuicOnlyTest, QuicListenerFactoryWithBadServerPreferredV4Address) {
  std::string yaml = TestEnvironment::substitute(R"EOF(
address:
  socket_address:
    address: 0.0.0.0
    protocol: UDP
    port_value: 1234
filter_chains:
- filter_chain_match:
    transport_protocol: "quic"
  name: foo
  filters:
  - name: envoy.filters.network.http_connection_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
      codec_type: HTTP3
      stat_prefix: hcm
      route_config:
        name: local_route
      http_filters:
        - name: envoy.filters.http.router
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  transport_socket:
    name: envoy.transport_sockets.quic
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicDownstreamTransport
      downstream_tls_context:
        common_tls_context:
          tls_certificates:
          - certificate_chain:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
            private_key:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
          validation_context:
            trusted_ca:
              filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
            match_typed_subject_alt_names:
            - matcher:
                exact: localhost
              san_type: URI
            - matcher:
                exact: 127.0.0.1
              san_type: IP_ADDRESS
udp_listener_config:
  quic_options:
    server_preferred_address_config:
      name: quic.server_preferred_address.fixed
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.quic.server_preferred_address.v3.FixedServerPreferredAddressConfig
        ipv4_address: "bad.v4.address"
  )EOF",
                                                 Network::Address::IpVersion::v4);
  envoy::config::listener::v3::Listener listener_proto = parseListenerFromV3Yaml(yaml);

#if defined(ENVOY_ENABLE_QUIC)
  EXPECT_THROW_WITH_REGEX(addOrUpdateListener(listener_proto), EnvoyException,
                          "bad v4 server preferred address");
#else
  EXPECT_THROW_WITH_REGEX(addOrUpdateListener(listener_proto), EnvoyException,
                          "QUIC is configured but not enabled in the build.");
#endif
}

TEST_P(ListenerManagerImplQuicOnlyTest, QuicListenerFactoryWithBadServerPreferredV6Address) {
  std::string yaml = TestEnvironment::substitute(R"EOF(
address:
  socket_address:
    address: 0.0.0.0
    protocol: UDP
    port_value: 1234
filter_chains:
- filter_chain_match:
    transport_protocol: "quic"
  name: foo
  filters:
  - name: envoy.filters.network.http_connection_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
      codec_type: HTTP3
      stat_prefix: hcm
      route_config:
        name: local_route
      http_filters:
        - name: envoy.filters.http.router
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  transport_socket:
    name: envoy.transport_sockets.quic
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicDownstreamTransport
      downstream_tls_context:
        common_tls_context:
          tls_certificates:
          - certificate_chain:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
            private_key:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
          validation_context:
            trusted_ca:
              filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
            match_typed_subject_alt_names:
            - matcher:
                exact: localhost
              san_type: URI
            - matcher:
                exact: 127.0.0.1
              san_type: IP_ADDRESS
udp_listener_config:
  quic_options:
    server_preferred_address_config:
      name: quic.server_preferred_address.fixed
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.quic.server_preferred_address.v3.FixedServerPreferredAddressConfig
        ipv6_address: "bad.v6.address"
  )EOF",
                                                 Network::Address::IpVersion::v4);
  envoy::config::listener::v3::Listener listener_proto = parseListenerFromV3Yaml(yaml);

#if defined(ENVOY_ENABLE_QUIC)
  EXPECT_THROW_WITH_REGEX(addOrUpdateListener(listener_proto), EnvoyException,
                          "bad v6 server preferred address");
#else
  EXPECT_THROW_WITH_REGEX(addOrUpdateListener(listener_proto), EnvoyException,
                          "QUIC is configured but not enabled in the build.");
#endif
}

INSTANTIATE_TEST_SUITE_P(Matcher, ListenerManagerImplQuicOnlyTest, ::testing::Values(false, true));

} // namespace
} // namespace Server
} // namespace Envoy
