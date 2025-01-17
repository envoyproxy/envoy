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
  HTTP1_WITH_TLS = 1,
  HTTP2_WITH_TLS = 2,
  HTTP3 = 3,
  HTTP_PROXY = 4,
  HTTPS_PROXY = 5,
};

class TestServer : public ListenerHooks {
public:
  TestServer();

  /**
   * Starts the test server. This function blocks until the test server is ready to accept requests.
   */
  void start(TestServerType type, int port = 0);

  /**
   * Shutdowns the server server. This function blocks until all the resources have been freed.
   */
  void shutdown();

  /** Gets the server address. The server address is a combination of IP address and port number. */
  std::string getAddress() const;

  /** Gets the server IP address. */
  std::string getIpAddress() const;

  /** Returns the server port number. */
  int getPort() const;

  /**
   * Sets headers and data for the test server to return on all future requests.
   * Can only be called once the server has been started.
   */
  [[deprecated("Use TestServer::setResponse instead")]] void
  setHeadersAndData(absl::string_view header_key, absl::string_view header_value,
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
  ProcessWide process_wide_;
  Thread::MutexBasicLockable lock_;
  Extensions::TransportSockets::Tls::ContextManagerImpl context_manager_{server_factory_context_};
  std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles> runfiles_;

  // Either test_server_ will be set for test_server_type is a proxy, otherwise upstream_ will be
  // used.
  std::unique_ptr<AutonomousUpstream> upstream_;
  IntegrationTestServerPtr test_server_;

  Network::DownstreamTransportSocketFactoryPtr createQuicUpstreamTlsContext(
      testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>&);

  Network::DownstreamTransportSocketFactoryPtr createUpstreamTlsContext(
      testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>&, bool);
};

} // namespace Envoy
