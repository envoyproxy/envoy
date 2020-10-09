#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"

#include "extensions/quic_listeners/quiche/quic_transport_socket_factory.h"

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
};

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
reuse_port: true
udp_listener_config:
  udp_listener_name: "quiche_quic_listener"
udp_writer_config:
  name: "udp_gso_batch_writer"
  typed_config:
    "@type": type.googleapis.com/envoy.config.listener.v3.UdpGsoBatchWriterOptions
  )EOF",
                                                       Network::Address::IpVersion::v4);

  envoy::config::listener::v3::Listener listener_proto = parseListenerFromV3Yaml(yaml);
  ON_CALL(udp_gso_syscall_, supportsUdpGso()).WillByDefault(Return(true));
  EXPECT_CALL(server_.api_.random_, uuid());
  expectCreateListenSocket(envoy::config::core::v3::SocketOption::STATE_PREBIND,
#ifdef SO_RXQ_OVFL // SO_REUSEPORT is on as configured
                           /* expected_num_options */
                           Api::OsSysCallsSingleton::get().supportsUdpGro() ? 4 : 3,
#else
                           /* expected_num_options */
                           Api::OsSysCallsSingleton::get().supportsUdpGro() ? 3 : 2,
#endif
                           /* expected_creation_params */ {true, false});

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
  if (Api::OsSysCallsSingleton::get().supportsUdpGro()) {
    expectSetsockopt(/* expected_sockopt_level */ SOL_UDP,
                     /* expected_sockopt_name */ UDP_GRO,
                     /* expected_value */ 1,
                     /* expected_num_calls */ 1);
  }

  manager_->addOrUpdateListener(listener_proto, "", true);
  EXPECT_EQ(1u, manager_->listeners().size());
  EXPECT_FALSE(manager_->listeners()[0].get().udpListenerFactory()->isTransportConnectionless());
  Network::SocketSharedPtr listen_socket =
      manager_->listeners().front().get().listenSocketFactory().getListenSocket();

  Network::UdpPacketWriterPtr udp_packet_writer =
      manager_->listeners().front().get().udpPacketWriterFactory()->get().createUdpPacketWriter(
          listen_socket->ioHandle(), manager_->listeners()[0].get().listenerScope());
  EXPECT_TRUE(udp_packet_writer->isBatchMode());

  // No filter chain found with non-matching transport protocol.
  EXPECT_EQ(nullptr, findFilterChain(1234, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111));

  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "quic", {}, "8.8.8.8", 111);
  ASSERT_NE(nullptr, filter_chain);
  auto& quic_socket_factory = dynamic_cast<const Quic::QuicServerTransportSocketFactory&>(
      filter_chain->transportSocketFactory());
  EXPECT_TRUE(quic_socket_factory.implementsSecureTransport());
  EXPECT_TRUE(quic_socket_factory.serverContextConfig().isReady());
}

} // namespace
} // namespace Server
} // namespace Envoy
