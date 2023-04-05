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
   */
  void startTestServer(bool use_quic);

  /**
   * Shutdowns the server. Can be restarted later. This is blocking until the server has freed all
   * resources.
   */
  void shutdownTestServer();

  /**
   * Returns the port that got attributed. Can only be called once the server has been started.
   */
  int getServerPort();
};

} // namespace Envoy
