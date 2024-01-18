#pragma once

#include "envoy/api/api.h"

#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/server.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/test_common/test_time.h"

#include "tools/cpp/runfiles/runfiles.h"

namespace Envoy {

/** An xDS test server. */
class XdsTestServer {
public:
  XdsTestServer();

  /** Starts the xDS server and returns the port number of the server. */
  void start();

  /** Gets the xDS host. */
  std::string getHost() const;

  /** Gets the xDS port. */
  int getPort() const;

  /** Sends a `DiscoveryResponse` from the xDS server. */
  void send(const envoy::service::discovery::v3::DiscoveryResponse& response);

  /** Shuts down the xDS server. */
  void shutdown();

private:
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  Stats::IsolatedStoreImpl stats_store_;
  Event::GlobalTimeSystem time_system_;
  Api::ApiPtr api_;
  Network::Address::IpVersion version_;
  MockBufferFactory* mock_buffer_factory_;
  Event::DispatcherPtr dispatcher_;
  FakeUpstreamConfig upstream_config_;
  Thread::MutexBasicLockable lock_;
  Extensions::TransportSockets::Tls::ContextManagerImpl context_manager_{time_system_};
  std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles> runfiles_;
  std::unique_ptr<FakeUpstream> xds_upstream_;
  FakeHttpConnectionPtr xds_connection_;
  FakeStreamPtr xds_stream_;
};

} // namespace Envoy
