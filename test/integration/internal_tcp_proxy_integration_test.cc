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

INSTANTIATE_TEST_SUITE_P(TcpProxyIntegrationTestParams,
                         ChainedProxyInternalTcpProxyIntegrationTest,
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

} // namespace Envoy
