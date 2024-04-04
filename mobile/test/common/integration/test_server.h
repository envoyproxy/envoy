#pragma once

#include "source/common/tls/ssl_socket.h"

// test_runner setups
#include "source/exe/process_wide.h"
#include "source/server/listener_hooks.h"

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "test/integration/autonomous_upstream.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/integration/server.h"

#include "tools/cpp/runfiles/runfiles.h"

namespace Envoy {

enum class TestServerType : int {
  HTTP1_WITHOUT_TLS = 0,
  HTTP2_WITH_TLS = 1,
  HTTP3 = 2,
  HTTP_PROXY = 3,
  HTTPS_PROXY = 4,
};

class TestServer : public ListenerHooks {
private:
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  Stats::IsolatedStoreImpl stats_store_;
  Event::GlobalTimeSystem time_system_;
  Api::ApiPtr api_;
  Network::Address::IpVersion version_;
  FakeUpstreamConfig upstream_config_;
  int port_;
  Thread::SkipAsserts skip_asserts_;
  ProcessWide process_wide;
  Thread::MutexBasicLockable lock;
  Extensions::TransportSockets::Tls::ContextManagerImpl context_manager_{server_factory_context_};
  std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles> runfiles_;

  // Either test_server_ will be set for test_server_type is a proxy, otherwise upstream_ will be
  // used.
  std::unique_ptr<AutonomousUpstream> upstream_;
  IntegrationTestServerPtr test_server_;

  Network::DownstreamTransportSocketFactoryPtr createQuicUpstreamTlsContext(
      testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>&);

  Network::DownstreamTransportSocketFactoryPtr createUpstreamTlsContext(
      testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>&);

  static const std::string http_proxy_config;
  static const std::string https_proxy_config;

public:
  TestServer();

  /**
   * Starts the server. Can only have one server active per JVM. This is blocking until the port can
   * start accepting requests.
   * test_server_type: selects between TestServerTypes
   */
  void startTestServer(TestServerType test_server_type);

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

  /**
   * Sets the response headers, body, and trailers for the test server to return
   * on all future request. Can only be called once the server has been started.
   */
  void setResponse(const absl::flat_hash_map<std::string, std::string>& headers,
                   absl::string_view body,
                   const absl::flat_hash_map<std::string, std::string>& trailers);

  // ListenerHooks
  void onWorkerListenerAdded() override {}
  void onWorkerListenerRemoved() override {}
  void onWorkersStarted() override {}
};

} // namespace Envoy
