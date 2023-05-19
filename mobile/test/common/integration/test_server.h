#pragma once

#include "source/extensions/transport_sockets/tls/ssl_socket.h"

// test_runner setups
#include "source/exe/process_wide.h"

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "test/integration/autonomous_upstream.h"
#include "test/mocks/server/transport_socket_factory_context.h"

namespace Envoy {
class TestServer {
private:
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  Stats::IsolatedStoreImpl stats_store_;
  Event::GlobalTimeSystem time_system_;
  Api::ApiPtr api_;
  Network::Address::IpVersion version_;
  std::unique_ptr<AutonomousUpstream> upstream_;
  FakeUpstreamConfig upstream_config_;
  int port_;
  Thread::SkipAsserts skip_asserts_;
  ProcessWide process_wide;
  Thread::MutexBasicLockable lock;
  std::string header_key_;
  std::string header_value_;
  std::string data_;
  Extensions::TransportSockets::Tls::ContextManagerImpl context_manager_{time_system_};

  Network::DownstreamTransportSocketFactoryPtr createQuicUpstreamTlsContext(
      testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>&);

  Network::DownstreamTransportSocketFactoryPtr createUpstreamTlsContext(
      testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>&);

public:
  TestServer();

  /**
   * Starts the server. Can only have one server active per JVM. This is blocking until the port can
   * start accepting requests.
   * use_quic and disable_https should not both be set to true.
   * use_quic: If true, use http3 with TLS.
   * disable_https: If true: use http1 without TLS. If false, use http2 with TLS.
   */
  void startTestServer(bool use_quic, bool disable_https);

  /**
   * Shutdowns the server. Can be restarted later. This is blocking until the server has freed all
   * resources.
   */
  void shutdownTestServer();

  /**
   * Returns the port that got attributed. Can only be called once the server has been started.
   */
  int getServerPort();

  /**
   * Sets headers and data for the test server to return on all future requests.
   * Can only be called once the server has been started.
   */
  void setHeadersAndData(absl::string_view header_key, absl::string_view header_value,
                         absl::string_view response_body);
};

} // namespace Envoy
