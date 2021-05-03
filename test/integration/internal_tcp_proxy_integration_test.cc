#include "test/integration/tcp_proxy_integration_test.h"

#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/network/address.h"

#include "common/config/api_version.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/integration/ssl_utility.h"
#include "test/integration/tcp_proxy_integration_test.pb.h"
#include "test/integration/tcp_proxy_integration_test.pb.validate.h"
#include "test/integration/utility.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

// tmp start
#include "extensions/io_socket/user_space/io_handle_impl.h"

using testing::_;
using testing::AtLeast;
using testing::HasSubstr;
using testing::Invoke;
using testing::MatchesRegex;
using testing::NiceMock;

namespace Envoy {

class InternalTcpProxyIntegrationTest
    : public testing::TestWithParam<TcpProxyIntegrationTestParams>,
      public BaseIntegrationTest {
public:
  InternalTcpProxyIntegrationTest()
      : BaseIntegrationTest(GetParam().version, internalTcpProxyConfig()) {
    enableHalfClose(true);
  }
  static std::string internalTcpProxyConfig() {
    return absl::StrCat(fmt::format(R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
dynamic_resources:
  lds_config:
    resource_api_version: V3
    path: {}
static_resources:
  secrets:
  - name: "secret_static_0"
    tls_certificate:
      certificate_chain:
        inline_string: "DUMMY_INLINE_BYTES"
      private_key:
        inline_string: "DUMMY_INLINE_BYTES"
      password:
        inline_string: "DUMMY_INLINE_BYTES"
  clusters:
  - name: cluster_0
    load_assignment:
      cluster_name: cluster_0
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
  - name: cluster_internal
    load_assignment:
      cluster_name: cluster_internal
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
#              pipe:
#                path: "@/tmp/pipetest"
              envoy_internal_address:
                server_listener_name: test_internal_listener_foo           
 
  listeners:
  - name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: tcp
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp_stats
          cluster: cluster_internal                   
  - name: listener_internal
    address:
#      pipe:
#        path: "@/tmp/pipetest"
      envoy_internal_address:
        server_listener_name: test_internal_listener_foo
    internal_listener: {{  }} # escape the bracket
)EOF",
                                    Platform::null_device_path, Platform::null_device_path),
                        R"EOF(
    filter_chains:
      filters:
        name: tcp
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp_stats
          cluster: cluster_0
)EOF");
  }

  void initialize() override;

  Event::Dispatcher* worker_dispatcher_{};
  int chained_number_{2};
};

std::vector<TcpProxyIntegrationTestParams> newPoolTestParams() {
  std::vector<TcpProxyIntegrationTestParams> ret;

  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    ret.push_back(TcpProxyIntegrationTestParams{ip_version, false});
  }
  return ret;
}

std::vector<TcpProxyIntegrationTestParams> getProtocolTestParams() {
  std::vector<TcpProxyIntegrationTestParams> ret;

  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    ret.push_back(TcpProxyIntegrationTestParams{ip_version, true});
    ret.push_back(TcpProxyIntegrationTestParams{ip_version, false});
  }
  return ret;
}

std::string
protocolTestParamsToString(const ::testing::TestParamInfo<TcpProxyIntegrationTestParams>& params) {
  return absl::StrCat(
      (params.param.version == Network::Address::IpVersion::v4 ? "IPv4_" : "IPv6_"),
      (params.param.test_original_version == true ? "OriginalConnPool" : "NewConnPool"));
}

void InternalTcpProxyIntegrationTest::initialize() {
  if (GetParam().test_original_version) {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.new_tcp_connection_pool", "false");
  } else {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.new_tcp_connection_pool", "true");
  }

  config_helper_.renameListener("tcp_proxy");
  BaseIntegrationTest::initialize();

  // worker_dispatcher_ can only be obtained after initialization.
  worker_dispatcher_ = getWorkerDispatcher(1);
  ASSERT(worker_dispatcher_ != nullptr);
}

INSTANTIATE_TEST_SUITE_P(TcpProxyIntegrationTestParams, InternalTcpProxyIntegrationTest,
                         testing::ValuesIn(getProtocolTestParams()), protocolTestParamsToString);
using ChainedProxyInternalTcpProxyIntegrationTest = InternalTcpProxyIntegrationTest;

INSTANTIATE_TEST_SUITE_P(TcpProxyIntegrationTestParams, ChainedProxyInternalTcpProxyIntegrationTest,
                         testing::ValuesIn(getProtocolTestParams()), protocolTestParamsToString);

// Test upstream writing before downstream downstream does.
TEST_P(InternalTcpProxyIntegrationTest, TcpProxyUpstreamWritesFirst) {
  initialize();

  // IntegrationTcpClientPtr tcp_client = std::make_unique<IntegrationTcpClient>(
  //     *dispatcher, *mock_buffer_factory_,
  //     std::make_shared<Network::Address::EnvoyInternalInstance>("hello"), enableHalfClose(),
  //     nullptr, nullptr);

  // lambda-expression cannot captures a structured binding so we must use tie() here.
  Network::IoHandlePtr io_handle_client;
  Network::IoHandlePtr io_handle_server;

  std::tie(io_handle_client, io_handle_server) =
      Extensions::IoSocket::UserSpace::IoHandleFactory::createIoHandlePair();

  auto server_address =
      std::make_shared<Network::Address::EnvoyInternalInstance>("test_internal_listener_foo");
  auto client_address = std::make_shared<Network::Address::EnvoyInternalInstance>("client_bar");

  worker_dispatcher_->post([&]() mutable {
    auto internal_listener =
        worker_dispatcher_->getInternalListenerManagerForTest().value().get().findByAddress(
            server_address);
    std::unique_ptr<Network::IoHandle> io_handle = std::move(io_handle_server);
    auto accepted_socket = std::make_unique<Network::AcceptedSocketImpl>(
        std::move(io_handle), server_address, client_address);
    internal_listener.value().get().onAccept(std::move(accepted_socket));
  });

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->write("hello"));
  // tcp_client->waitForData("hello");
  // // Make sure inexact matches work also on data already received.
  // tcp_client->waitForData("ello", false);

  // // Make sure length based wait works for the data already received
  // ASSERT_TRUE(tcp_client->waitForData(5));
  // ASSERT_TRUE(tcp_client->waitForData(4));
  Buffer::OwnedImpl buffer;
  Thread::MutexBasicLockable dispatcher_mutex;

  while (buffer.length() < 5) {
    Thread::CondVar post_complete;
    worker_dispatcher_->post([&]() {
      Thread::LockGuard guard(dispatcher_mutex);
      io_handle_client->read(buffer, absl::nullopt);
      post_complete.notifyOne();
    });

    Thread::LockGuard guard(dispatcher_mutex);
    post_complete.wait(dispatcher_mutex);
    ENVOY_LOG_MISC(debug, "current buffer: {}", buffer.toString());
  }

  // tmp start
  ASSERT_TRUE(fake_upstream_connection->write("", true));
  // ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  // ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  // // Drain part of the received message
  // tcp_client->clearData(2);
  // tcp_client->waitForData("llo");
  // ASSERT_TRUE(tcp_client->waitForData(3));

  // ASSERT_TRUE(tcp_client->write("hello"));
  // ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // ASSERT_TRUE(fake_upstream_connection->write("", true));
  // tcp_client->waitForHalfClose();
  // ASSERT_TRUE(tcp_client->write("", true));
  // ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  // ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  // // Any time an associated connection is destroyed, it increments both counters.
  // test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_destroy", 1);
  // test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_destroy_with_active_rq", 1);

  // IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("tcp_proxy"));
  // FakeRawConnectionPtr fake_upstream_connection2;
  // ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection2));
  // tcp_client2->close();
}

// Test upstream writing before downstream downstream does.
TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, TcpProxyUpstreamWritesFirst) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->write("hello"));
  tcp_client->waitForData("hello");
  // Make sure inexact matches work also on data already received.
  tcp_client->waitForData("ello", false);

  // Make sure length based wait works for the data already received
  ASSERT_TRUE(tcp_client->waitForData(5));
  ASSERT_TRUE(tcp_client->waitForData(4));

  // Drain part of the received message
  tcp_client->clearData(2);
  tcp_client->waitForData("llo");
  ASSERT_TRUE(tcp_client->waitForData(3));

  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  ASSERT_TRUE(fake_upstream_connection->write("", true));
  tcp_client->waitForHalfClose();
  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  // Any time an associated connection is destroyed, it increments both counters.
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_destroy", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_destroy_with_active_rq", 1);

  IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection2));
  tcp_client2->close();
}

// Test TLS upstream.
TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, TcpProxyUpstreamTls) {
  upstream_tls_ = true;
  setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);
  config_helper_.configureUpstreamTls();
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForHalfClose();
  tcp_client->close();

  EXPECT_EQ("world", tcp_client->data());
  // Any time an associated connection is destroyed, it increments both counters.
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_destroy", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_destroy_with_active_rq", 1);
}

// Test proxying data in both directions, and that all data is flushed properly
// when there is an upstream disconnect.
TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, TcpProxyUpstreamDisconnect) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForHalfClose();
  tcp_client->close();

  EXPECT_EQ("world", tcp_client->data());
}

// Test proxying data in both directions, and that all data is flushed properly
// when the client disconnects.
TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, TcpProxyDownstreamDisconnect) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");
  ASSERT_TRUE(tcp_client->write("hello", true));
  ASSERT_TRUE(fake_upstream_connection->waitForData(10));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForDisconnect();
}

TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, TcpProxyManyConnections) {
  autonomous_upstream_ = true;
  initialize();
  const int num_connections = 50;
  std::vector<IntegrationTcpClientPtr> clients(num_connections);

  for (int i = 0; i < num_connections; ++i) {
    clients[i] = makeTcpConnection(lookupPort("tcp_proxy"));
  }
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", num_connections);
  for (int i = 0; i < num_connections; ++i) {
    IntegrationTcpClientPtr& tcp_client = clients[i];
    // The autonomous upstream is an HTTP upstream, so send raw HTTP.
    // This particular request will result in the upstream sending a response,
    // and flush-closing due to the 'close_after_response' header.
    ASSERT_TRUE(tcp_client->write(
        "GET / HTTP/1.1\r\nHost: foo\r\nclose_after_response: yes\r\ncontent-length: 0\r\n\r\n",
        false));
    tcp_client->waitForHalfClose();
    tcp_client->close();
    EXPECT_THAT(tcp_client->data(), HasSubstr("aaaaaaaaaa"));
  }
}

TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, TcpProxyRandomBehavior) {
  autonomous_upstream_ = true;
  initialize();
  std::list<IntegrationTcpClientPtr> clients;

  // The autonomous upstream parses HTTP, and HTTP headers and sends responses
  // when full requests are received. basic_request will result in
  // bidirectional data. request_with_close will result in bidirectional data,
  // but also the upstream closing the connection.
  const char* basic_request = "GET / HTTP/1.1\r\nHost: foo\r\ncontent-length: 0\r\n\r\n";
  const char* request_with_close =
      "GET / HTTP/1.1\r\nHost: foo\r\nclose_after_response: yes\r\ncontent-length: 0\r\n\r\n";
  TestRandomGenerator rand;

  // Seed some initial clients
  for (int i = 0; i < 5; ++i) {
    clients.push_back(makeTcpConnection(lookupPort("tcp_proxy")));
  }

  // Now randomly write / add more connections / close.
  for (int i = 0; i < 50; ++i) {
    int action = rand.random() % 3;

    if (action == 0) {
      // Add a new connection.
      clients.push_back(makeTcpConnection(lookupPort("tcp_proxy")));
    }
    if (clients.empty()) {
      break;
    }
    IntegrationTcpClientPtr& tcp_client = clients.front();
    if (action == 1) {
      // Write to the first connection.
      ASSERT_TRUE(tcp_client->write(basic_request, false));
      tcp_client->waitForData("\r\n\r\n", false);
      tcp_client->clearData(tcp_client->data().size());
    } else if (action == 2) {
      // Close the first connection.
      ASSERT_TRUE(tcp_client->write(request_with_close, false));
      tcp_client->waitForData("\r\n\r\n", false);
      tcp_client->waitForHalfClose();
      tcp_client->close();
      clients.pop_front();
    }
  }

  while (!clients.empty()) {
    IntegrationTcpClientPtr& tcp_client = clients.front();
    ASSERT_TRUE(tcp_client->write(request_with_close, false));
    tcp_client->waitForData("\r\n\r\n", false);
    tcp_client->waitForHalfClose();
    tcp_client->close();
    clients.pop_front();
  }
}

TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, NoUpstream) {
  // Set the first upstream to have an invalid port, so connection will fail,
  // but it won't fail synchronously (as it would if there were simply no
  // upstreams)
  fake_upstreams_count_ = 0;
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto* lb_endpoint =
        cluster->mutable_load_assignment()->mutable_endpoints(0)->mutable_lb_endpoints(0);
    lb_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(1);
  });
  config_helper_.skipPortUsageValidation();
  enableHalfClose(false);
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  tcp_client->waitForDisconnect();
}

TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, TcpProxyLargeWrite) {
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  std::string data(1024 * 16, 'a');
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write(data));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(data.size()));
  ASSERT_TRUE(fake_upstream_connection->write(data));
  tcp_client->waitForData(data);
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  uint32_t upstream_pauses =
      test_server_->counter("cluster.cluster_0.upstream_flow_control_paused_reading_total")
          ->value();
  uint32_t upstream_resumes =
      test_server_->counter("cluster.cluster_0.upstream_flow_control_resumed_reading_total")
          ->value();
  EXPECT_EQ(upstream_pauses, upstream_resumes);

  uint32_t downstream_pauses =
      test_server_->counter("tcp.tcp_stats.downstream_flow_control_paused_reading_total")->value();
  uint32_t downstream_resumes =
      test_server_->counter("tcp.tcp_stats.downstream_flow_control_resumed_reading_total")->value();
  EXPECT_EQ(downstream_pauses, downstream_resumes);
}

// Test that a downstream flush works correctly (all data is flushed)
TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, TcpProxyDownstreamFlush) {
  // Use a very large size to make sure it is larger than the kernel socket read buffer.
  const uint32_t size = 50 * 1024 * 1024;
  config_helper_.setBufferLimits(size / 8, size / 8);
  initialize();

  std::string data(size, 'a');
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  tcp_client->readDisable(true);
  ASSERT_TRUE(tcp_client->write("", true));

  // This ensures that readDisable(true) has been run on it's thread
  // before tcp_client starts writing.
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  ASSERT_TRUE(fake_upstream_connection->write(data, true));

  // test_server_->waitForCounterGe("cluster.cluster_0.upstream_flow_control_paused_reading_total",
  // 1);
  // EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_flow_control_resumed_reading_total")
  //               ->value(),
  //           0);
  tcp_client->readDisable(false);
  tcp_client->waitForData(data);
  tcp_client->waitForHalfClose();
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  uint32_t upstream_pauses =
      test_server_->counter("cluster.cluster_0.upstream_flow_control_paused_reading_total")
          ->value();
  uint32_t upstream_resumes =
      test_server_->counter("cluster.cluster_0.upstream_flow_control_resumed_reading_total")
          ->value();
  EXPECT_GE(upstream_pauses, upstream_resumes);
  EXPECT_GT(upstream_resumes, 0);
}

// Test that an upstream flush works correctly (all data is flushed)
TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, TcpProxyUpstreamFlush) {
  // Use a very large size to make sure it is larger than the kernel socket read buffer.
  const uint32_t size = 50 * 1024 * 1024;
  config_helper_.setBufferLimits(size / 8, size / 8);
  initialize();

  std::string data(size, 'a');
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->readDisable(true));
  ASSERT_TRUE(fake_upstream_connection->write("", true));

  // This ensures that fake_upstream_connection->readDisable has been run on it's thread
  // before tcp_client starts writing.
  tcp_client->waitForHalfClose();

  ASSERT_TRUE(tcp_client->write(data, true, true, std::chrono::milliseconds(30000)));

  test_server_->waitForGaugeEq("tcp.tcp_stats.upstream_flush_active", 1);
  ASSERT_TRUE(fake_upstream_connection->readDisable(false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(data.size()));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForHalfClose();

  // Double because two tcp proxy are chained.
  EXPECT_EQ(test_server_->counter("tcp.tcp_stats.upstream_flush_total")->value(),
            1 * chained_number_);
  test_server_->waitForGaugeEq("tcp.tcp_stats.upstream_flush_active", 0);
}

// Test that Envoy doesn't crash or assert when shutting down with an upstream flush active
TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, TcpProxyUpstreamFlushEnvoyExit) {
  // Use a very large size to make sure it is larger than the kernel socket read buffer.
  const uint32_t size = 50 * 1024 * 1024;
  config_helper_.setBufferLimits(size, size);
  initialize();

  std::string data(size, 'a');
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->readDisable(true));
  ASSERT_TRUE(fake_upstream_connection->write("", true));

  // This ensures that fake_upstream_connection->readDisable has been run on it's thread
  // before tcp_client starts writing.
  tcp_client->waitForHalfClose();

  ASSERT_TRUE(tcp_client->write(data, true));

  test_server_->waitForGaugeEq("tcp.tcp_stats.upstream_flush_active", 1);
  test_server_.reset();
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  // Success criteria is that no ASSERTs fire and there are no leaks.
}

TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, AccessLog) {
  std::string access_log_path = TestEnvironment::temporaryPath(
      fmt::format("access_log{}.txt", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6"));
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>());
    auto tcp_proxy_config =
        MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
            *config_blob);

    auto* access_log = tcp_proxy_config.add_access_log();
    access_log->set_name("accesslog");
    envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
    access_log_config.set_path(access_log_path);
    access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        "upstreamlocal=%UPSTREAM_LOCAL_ADDRESS% "
        "upstreamhost=%UPSTREAM_HOST% downstream=%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT% "
        "sent=%BYTES_SENT% received=%BYTES_RECEIVED%\n");
    access_log->mutable_typed_config()->PackFrom(access_log_config);
    auto* runtime_filter = access_log->mutable_filter()->mutable_runtime_filter();
    runtime_filter->set_runtime_key("unused-key");
    auto* percent_sampled = runtime_filter->mutable_percent_sampled();
    percent_sampled->set_numerator(100);
    percent_sampled->set_denominator(envoy::type::v3::FractionalPercent::DenominatorType::
                                         FractionalPercent_DenominatorType_HUNDRED);
    config_blob->PackFrom(tcp_proxy_config);
  });
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->write("hello"));
  tcp_client->waitForData("hello");

  ASSERT_TRUE(fake_upstream_connection->write("", true));
  tcp_client->waitForHalfClose();
  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  std::string log_result;
  // Access logs only get flushed to disk periodically, so poll until the log is non-empty
  do {
    log_result = api_->fileSystem().fileReadToEnd(access_log_path);
  } while (log_result.empty());

  // Regex matching localhost:port
#ifndef GTEST_USES_SIMPLE_RE
  const std::string ip_port_regex = (version_ == Network::Address::IpVersion::v4)
                                        ? R"EOF(127\.0\.0\.1:[0-9]+)EOF"
                                        : R"EOF(\[::1\]:[0-9]+)EOF";
#else
  const std::string ip_port_regex = (version_ == Network::Address::IpVersion::v4)
                                        ? R"EOF(127\.0\.0\.1:\d+)EOF"
                                        : R"EOF(\[::1\]:\d+)EOF";
#endif

  const std::string ip_regex =
      (version_ == Network::Address::IpVersion::v4) ? R"EOF(127\.0\.0\.1)EOF" : R"EOF(::1)EOF";

  // Test that all three addresses were populated correctly. Only check the first line of
  // log output for simplicity.
  EXPECT_THAT(log_result,
              MatchesRegex(fmt::format(
                  "upstreamlocal={0} upstreamhost={2} downstream={1} sent=5 received=0\r?\n.*",
                  // Upstream local is not defined for internal connection.
                  "-", ip_regex,
                  // Upstream remote address is formatted as internal listener name.
                  "envoy://test_internal_listener_foo")));
}

// Test that the server shuts down without crashing when connections are open.
TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, ShutdownWithOpenConnections) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* static_resources = bootstrap.mutable_static_resources();
    for (int i = 0; i < static_resources->clusters_size(); ++i) {
      auto* cluster = static_resources->mutable_clusters(i);
      cluster->set_close_connections_on_host_health_failure(true);
    }
  });
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");
  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(10));
  test_server_.reset();
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForHalfClose();
  tcp_client->close();

  // Success criteria is that no ASSERTs fire and there are no leaks.
}

TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, TestIdletimeoutWithNoData) {
  autonomous_upstream_ = true;

  enableHalfClose(false);
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>());
    auto tcp_proxy_config =
        MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
            *config_blob);
    tcp_proxy_config.mutable_idle_timeout()->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(100))
            .count());
    config_blob->PackFrom(tcp_proxy_config);
  });

  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  tcp_client->waitForDisconnect();
}

TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, TestIdletimeoutWithLargeOutstandingData) {
  config_helper_.setBufferLimits(1024, 1024);
  enableHalfClose(false);
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>());
    auto tcp_proxy_config =
        MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
            *config_blob);
    tcp_proxy_config.mutable_idle_timeout()->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(500))
            .count());
    config_blob->PackFrom(tcp_proxy_config);
  });

  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  std::string data(1024 * 16, 'a');
  ASSERT_TRUE(tcp_client->write(data));
  ASSERT_TRUE(fake_upstream_connection->write(data));

  tcp_client->waitForDisconnect();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, TestMaxDownstreamConnectionDurationWithNoData) {
  autonomous_upstream_ = true;

  enableHalfClose(false);
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>());
    auto tcp_proxy_config =
        MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
            *config_blob);
    tcp_proxy_config.mutable_max_downstream_connection_duration()->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(100))
            .count());
    config_blob->PackFrom(tcp_proxy_config);
  });

  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  tcp_client->waitForDisconnect();
}

TEST_P(ChainedProxyInternalTcpProxyIntegrationTest,
       TestMaxDownstreamConnectionDurationWithLargeOutstandingData) {
  config_helper_.setBufferLimits(1024, 1024);
  enableHalfClose(false);
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>());
    auto tcp_proxy_config =
        MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
            *config_blob);
    tcp_proxy_config.mutable_max_downstream_connection_duration()->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(500))
            .count());
    config_blob->PackFrom(tcp_proxy_config);
  });

  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  std::string data(1024 * 16, 'a');
  ASSERT_TRUE(tcp_client->write(data));
  ASSERT_TRUE(fake_upstream_connection->write(data));

  tcp_client->waitForDisconnect();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, TestNoCloseOnHealthFailure) {
  concurrency_ = 2;

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* static_resources = bootstrap.mutable_static_resources();
    for (int i = 0; i < static_resources->clusters_size(); ++i) {
      auto* cluster = static_resources->mutable_clusters(i);
      cluster->set_close_connections_on_host_health_failure(false);
      cluster->mutable_common_lb_config()->mutable_healthy_panic_threshold()->set_value(0);
      cluster->add_health_checks()->mutable_timeout()->set_seconds(20);
      cluster->mutable_health_checks(0)->mutable_reuse_connection()->set_value(true);
      cluster->mutable_health_checks(0)->mutable_interval()->set_seconds(1);
      cluster->mutable_health_checks(0)->mutable_no_traffic_interval()->set_seconds(1);
      cluster->mutable_health_checks(0)->mutable_unhealthy_threshold()->set_value(1);
      cluster->mutable_health_checks(0)->mutable_healthy_threshold()->set_value(1);
      cluster->mutable_health_checks(0)->mutable_tcp_health_check();
      cluster->mutable_health_checks(0)->mutable_tcp_health_check()->mutable_send()->set_text(
          "50696E67");
      cluster->mutable_health_checks(0)->mutable_tcp_health_check()->add_receive()->set_text(
          "506F6E67");
    }
  });

  FakeRawConnectionPtr fake_upstream_health_connection;
  on_server_init_function_ = [&](void) -> void {
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_health_connection));
    ASSERT_TRUE(fake_upstream_health_connection->waitForData(
        FakeRawConnection::waitForInexactMatch("Ping")));
    ASSERT_TRUE(fake_upstream_health_connection->write("Pong"));
  };

  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(10));

  ASSERT_TRUE(fake_upstream_health_connection->waitForData(8));
  ASSERT_TRUE(fake_upstream_health_connection->close());
  ASSERT_TRUE(fake_upstream_health_connection->waitForDisconnect());

  // By waiting we know the previous health check attempt completed (with a failure since we closed
  // the connection on it)
  FakeRawConnectionPtr fake_upstream_health_connection_reconnect;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_health_connection_reconnect));
  ASSERT_TRUE(fake_upstream_health_connection_reconnect->waitForData(
      FakeRawConnection::waitForInexactMatch("Ping")));

  ASSERT_TRUE(tcp_client->write("still"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(15));
  ASSERT_TRUE(fake_upstream_connection->write("here"));
  tcp_client->waitForData("here", false);

  test_server_.reset();
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  ASSERT_TRUE(fake_upstream_health_connection_reconnect->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_health_connection_reconnect->close());
  ASSERT_TRUE(fake_upstream_health_connection_reconnect->waitForDisconnect());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(ChainedProxyInternalTcpProxyIntegrationTest, TestCloseOnHealthFailure) {
  concurrency_ = 2;

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* static_resources = bootstrap.mutable_static_resources();
    for (int i = 0; i < static_resources->clusters_size(); ++i) {
      auto* cluster = static_resources->mutable_clusters(i);
      cluster->set_close_connections_on_host_health_failure(true);
      cluster->mutable_common_lb_config()->mutable_healthy_panic_threshold()->set_value(0);
      cluster->add_health_checks()->mutable_timeout()->set_seconds(20);
      cluster->mutable_health_checks(0)->mutable_reuse_connection()->set_value(true);
      cluster->mutable_health_checks(0)->mutable_interval()->set_seconds(1);
      cluster->mutable_health_checks(0)->mutable_no_traffic_interval()->set_seconds(1);
      cluster->mutable_health_checks(0)->mutable_unhealthy_threshold()->set_value(1);
      cluster->mutable_health_checks(0)->mutable_healthy_threshold()->set_value(1);
      cluster->mutable_health_checks(0)->mutable_tcp_health_check();
      cluster->mutable_health_checks(0)->mutable_tcp_health_check()->mutable_send()->set_text(
          "50696E67");
      ;
      cluster->mutable_health_checks(0)->mutable_tcp_health_check()->add_receive()->set_text(
          "506F6E67");
    }
  });

  FakeRawConnectionPtr fake_upstream_health_connection;
  on_server_init_function_ = [&](void) -> void {
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_health_connection));
    ASSERT_TRUE(fake_upstream_health_connection->waitForData(4));
    ASSERT_TRUE(fake_upstream_health_connection->write("Pong"));
  };

  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(10));

  ASSERT_TRUE(fake_upstream_health_connection->waitForData(8));
  ASSERT_TRUE(fake_upstream_health_connection->close());
  ASSERT_TRUE(fake_upstream_health_connection->waitForDisconnect());

  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  tcp_client->waitForHalfClose();

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

} // namespace Envoy
