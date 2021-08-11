#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"

#if defined(ENVOY_ENABLE_QUIC)
#include "source/common/quic/quic_transport_socket_factory.h"
#endif

#include "test/server/listener_manager_impl_test.h"
#include "test/server/utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"

namespace Envoy {
namespace Server {
namespace {

class MockSupportsUdpGso : public Api::OsSysCallsImpl {
public:
  MOCK_METHOD(bool, supportsUdpGso, (), (const));
};

class ListenerManagerImplQuicOnlyTest : public ListenerManagerImplTest {
public:
  NiceMock<MockSupportsUdpGso> udp_gso_syscall_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&udp_gso_syscall_};
  Api::OsSysCallsImpl os_sys_calls_actual_;
};

#if defined(ENVOY_ENABLE_QUIC)
TEST_F(ListenerManagerImplQuicOnlyTest, QuicListenerFactoryAndSslContext) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    protocol: UDP
    port_value: 1234
filter_chains:
- filter_chain_match:
    transport_protocol: "quic"
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
              filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
            private_key:
              filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
          validation_context:
            trusted_ca:
              filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
            match_subject_alt_names:
            - exact: localhost
            - exact: 127.0.0.1
udp_listener_config:
  quic_options: {}
  )EOF",
                                                       Network::Address::IpVersion::v4);

  envoy::config::listener::v3::Listener listener_proto = parseListenerFromV3Yaml(yaml);
  ON_CALL(udp_gso_syscall_, supportsUdpGso())
      .WillByDefault(Return(os_sys_calls_actual_.supportsUdpGso()));
  EXPECT_CALL(server_.api_.random_, uuid());
  expectCreateListenSocket(envoy::config::core::v3::SocketOption::STATE_PREBIND,
#ifdef SO_RXQ_OVFL // SO_REUSEPORT is on as configured
                           /* expected_num_options */
                           Api::OsSysCallsSingleton::get().supportsUdpGro() ? 4 : 3,
#else
                           /* expected_num_options */
                           Api::OsSysCallsSingleton::get().supportsUdpGro() ? 3 : 2,
#endif
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

  manager_->addOrUpdateListener(listener_proto, "", true);
  EXPECT_EQ(1u, manager_->listeners().size());
  EXPECT_FALSE(manager_->listeners()[0]
                   .get()
                   .udpListenerConfig()
                   ->listenerFactory()
                   .isTransportConnectionless());
  Network::SocketSharedPtr listen_socket =
      manager_->listeners().front().get().listenSocketFactory().getListenSocket(0);

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
  EXPECT_FALSE(quic_socket_factory.getTlsCertificates().empty());
}
#endif

TEST_F(ListenerManagerImplQuicOnlyTest, QuicListenerFactoryWithWrongTransportSocket) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    protocol: UDP
    port_value: 1234
filter_chains:
- filter_chain_match:
    transport_protocol: "quic"
  filters: []
  transport_socket:
    name: envoy.transport_sockets.quic
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
      common_tls_context:
        tls_certificates:
        - certificate_chain:
            filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
          private_key:
            filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
        validation_context:
          trusted_ca:
            filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
          match_subject_alt_names:
          - exact: localhost
          - exact: 127.0.0.1
udp_listener_config:
  quic_options: {}
  )EOF",
                                                       Network::Address::IpVersion::v4);

  envoy::config::listener::v3::Listener listener_proto = parseListenerFromV3Yaml(yaml);

#if defined(ENVOY_ENABLE_QUIC)
  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(listener_proto, "", true), EnvoyException,
                          "wrong transport socket config specified for quic transport socket");
#else
  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(listener_proto, "", true), EnvoyException,
                          "QUIC is configured but not enabled in the build.");
#endif
}

TEST_F(ListenerManagerImplQuicOnlyTest, QuicListenerFactoryWithWrongCodec) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    protocol: UDP
    port_value: 1234
filter_chains:
- filter_chain_match:
    transport_protocol: "quic"
  filters: []
  transport_socket:
    name: envoy.transport_sockets.quic
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicDownstreamTransport
      downstream_tls_context:
        common_tls_context:
          tls_certificates:
          - certificate_chain:
              filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
            private_key:
              filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
          validation_context:
            trusted_ca:
              filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
            match_subject_alt_names:
            - exact: localhost
            - exact: 127.0.0.1
udp_listener_config:
  quic_options: {}
  )EOF",
                                                       Network::Address::IpVersion::v4);

  envoy::config::listener::v3::Listener listener_proto = parseListenerFromV3Yaml(yaml);

#if defined(ENVOY_ENABLE_QUIC)
  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(listener_proto, "", true), EnvoyException,
                          "error building network filter chain for quic listener: requires exactly "
                          "one http_connection_manager filter.");
#else
  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(listener_proto, "", true), EnvoyException,
                          "QUIC is configured but not enabled in the build.");
#endif
}

} // namespace
} // namespace Server
} // namespace Envoy
