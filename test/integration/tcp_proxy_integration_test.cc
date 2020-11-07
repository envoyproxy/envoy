#include "test/integration/tcp_proxy_integration_test.h"

#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "common/config/api_version.h"
#include "common/network/utility.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/integration/ssl_utility.h"
#include "test/integration/tcp_proxy_integration_test.pb.h"
#include "test/integration/tcp_proxy_integration_test.pb.validate.h"
#include "test/integration/utility.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::MatchesRegex;
using testing::NiceMock;

namespace Envoy {

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

void TcpProxyIntegrationTest::initialize() {
  if (GetParam().test_original_version) {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.new_tcp_connection_pool", "false");
  } else {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.new_tcp_connection_pool", "true");
  }

  config_helper_.renameListener("tcp_proxy");
  BaseIntegrationTest::initialize();
}

INSTANTIATE_TEST_SUITE_P(TcpProxyIntegrationTestParams, TcpProxyIntegrationTest,
                         testing::ValuesIn(getProtocolTestParams()), protocolTestParamsToString);

// Test upstream writing before downstream downstream does.
TEST_P(TcpProxyIntegrationTest, TcpProxyUpstreamWritesFirst) {
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
}

// Test proxying data in both directions, and that all data is flushed properly
// when there is an upstream disconnect.
TEST_P(TcpProxyIntegrationTest, TcpProxyUpstreamDisconnect) {
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
TEST_P(TcpProxyIntegrationTest, TcpProxyDownstreamDisconnect) {
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

TEST_P(TcpProxyIntegrationTest, NoUpstream) {
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
  enable_half_close_ = false;
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  tcp_client->waitForDisconnect();
}

TEST_P(TcpProxyIntegrationTest, TcpProxyLargeWrite) {
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
TEST_P(TcpProxyIntegrationTest, TcpProxyDownstreamFlush) {
  // Use a very large size to make sure it is larger than the kernel socket read buffer.
  const uint32_t size = 50 * 1024 * 1024;
  config_helper_.setBufferLimits(size / 4, size / 4);
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

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_flow_control_paused_reading_total", 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_flow_control_resumed_reading_total")
                ->value(),
            0);
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
TEST_P(TcpProxyIntegrationTest, TcpProxyUpstreamFlush) {
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

  ASSERT_TRUE(tcp_client->write(data, true, true, std::chrono::milliseconds(30000)));

  test_server_->waitForGaugeEq("tcp.tcp_stats.upstream_flush_active", 1);
  ASSERT_TRUE(fake_upstream_connection->readDisable(false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(data.size()));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForHalfClose();

  EXPECT_EQ(test_server_->counter("tcp.tcp_stats.upstream_flush_total")->value(), 1);
  test_server_->waitForGaugeEq("tcp.tcp_stats.upstream_flush_active", 0);
}

// Test that Envoy doesn't crash or assert when shutting down with an upstream flush active
TEST_P(TcpProxyIntegrationTest, TcpProxyUpstreamFlushEnvoyExit) {
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

TEST_P(TcpProxyIntegrationTest, AccessLog) {
  std::string access_log_path = TestEnvironment::temporaryPath(
      fmt::format("access_log{}.txt", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6"));
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(
        config_blob->Is<API_NO_BOOST(envoy::config::filter::network::tcp_proxy::v2::TcpProxy)>());
    auto tcp_proxy_config = MessageUtil::anyConvert<API_NO_BOOST(
        envoy::config::filter::network::tcp_proxy::v2::TcpProxy)>(*config_blob);

    auto* access_log = tcp_proxy_config.add_access_log();
    access_log->set_name("accesslog");
    envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
    access_log_config.set_path(access_log_path);
    access_log_config.mutable_log_format()->set_text_format(
        "upstreamlocal=%UPSTREAM_LOCAL_ADDRESS% "
        "upstreamhost=%UPSTREAM_HOST% downstream=%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT% "
        "sent=%BYTES_SENT% received=%BYTES_RECEIVED%\n");
    access_log->mutable_typed_config()->PackFrom(access_log_config);
    auto* runtime_filter = access_log->mutable_filter()->mutable_runtime_filter();
    runtime_filter->set_runtime_key("unused-key");
    auto* percent_sampled = runtime_filter->mutable_percent_sampled();
    percent_sampled->set_numerator(100);
    percent_sampled->set_denominator(
        envoy::type::FractionalPercent::DenominatorType::FractionalPercent_DenominatorType_HUNDRED);
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
                  "upstreamlocal={0} upstreamhost={0} downstream={1} sent=5 received=0\r?\n.*",
                  ip_port_regex, ip_regex)));
}

// Test that the server shuts down without crashing when connections are open.
TEST_P(TcpProxyIntegrationTest, ShutdownWithOpenConnections) {
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

TEST_P(TcpProxyIntegrationTest, TestIdletimeoutWithNoData) {
  autonomous_upstream_ = true;

  enable_half_close_ = false;
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(
        config_blob->Is<API_NO_BOOST(envoy::config::filter::network::tcp_proxy::v2::TcpProxy)>());
    auto tcp_proxy_config = MessageUtil::anyConvert<API_NO_BOOST(
        envoy::config::filter::network::tcp_proxy::v2::TcpProxy)>(*config_blob);
    tcp_proxy_config.mutable_idle_timeout()->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(100))
            .count());
    config_blob->PackFrom(tcp_proxy_config);
  });

  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  tcp_client->waitForDisconnect();
}

TEST_P(TcpProxyIntegrationTest, TestIdletimeoutWithLargeOutstandingData) {
  config_helper_.setBufferLimits(1024, 1024);
  enable_half_close_ = false;
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(
        config_blob->Is<API_NO_BOOST(envoy::config::filter::network::tcp_proxy::v2::TcpProxy)>());
    auto tcp_proxy_config = MessageUtil::anyConvert<API_NO_BOOST(
        envoy::config::filter::network::tcp_proxy::v2::TcpProxy)>(*config_blob);
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

TEST_P(TcpProxyIntegrationTest, TestMaxDownstreamConnectionDurationWithNoData) {
  autonomous_upstream_ = true;

  enable_half_close_ = false;
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(
        config_blob->Is<API_NO_BOOST(envoy::config::filter::network::tcp_proxy::v2::TcpProxy)>());
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

TEST_P(TcpProxyIntegrationTest, TestMaxDownstreamConnectionDurationWithLargeOutstandingData) {
  config_helper_.setBufferLimits(1024, 1024);
  enable_half_close_ = false;
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(
        config_blob->Is<API_NO_BOOST(envoy::config::filter::network::tcp_proxy::v2::TcpProxy)>());
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

TEST_P(TcpProxyIntegrationTest, TestNoCloseOnHealthFailure) {
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

TEST_P(TcpProxyIntegrationTest, TestCloseOnHealthFailure) {
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

class TcpProxyMetadataMatchIntegrationTest : public TcpProxyIntegrationTest {
public:
  TcpProxyMetadataMatchIntegrationTest(uint32_t tcp_proxy_filter_index = 0)
      : tcp_proxy_filter_index_(tcp_proxy_filter_index) {}
  void initialize() override;

  void expectEndpointToMatchRoute(
      std::function<std::string(IntegrationTcpClient&)> initial_data_cb = nullptr);
  void expectEndpointNotToMatchRoute(const std::string& write_data = "hello");

  envoy::config::core::v3::Metadata lbMetadata(std::map<std::string, std::string> values);

  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy_;
  envoy::config::core::v3::Metadata endpoint_metadata_;
  const uint32_t tcp_proxy_filter_index_;
};

envoy::config::core::v3::Metadata
TcpProxyMetadataMatchIntegrationTest::lbMetadata(std::map<std::string, std::string> values) {

  ProtobufWkt::Struct map;
  auto* mutable_fields = map.mutable_fields();
  ProtobufWkt::Value value;

  std::map<std::string, std::string>::iterator it;
  for (it = values.begin(); it != values.end(); it++) {
    value.set_string_value(it->second);
    mutable_fields->insert({it->first, value});
  }

  envoy::config::core::v3::Metadata metadata;
  (*metadata.mutable_filter_metadata())[Envoy::Config::MetadataFilters::get().ENVOY_LB] = map;
  return metadata;
}

void TcpProxyMetadataMatchIntegrationTest::initialize() {

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();

    ASSERT(static_resources->listeners_size() == 1);
    static_resources->mutable_listeners(0)
        ->mutable_filter_chains(0)
        ->mutable_filters(tcp_proxy_filter_index_)
        ->mutable_typed_config()
        ->PackFrom(tcp_proxy_);

    ASSERT(static_resources->clusters_size() == 1);
    auto* cluster_0 = static_resources->mutable_clusters(0);
    cluster_0->Clear();
    cluster_0->set_name("cluster_0");
    cluster_0->set_type(envoy::config::cluster::v3::Cluster::STATIC);
    cluster_0->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
    auto* lb_subset_config = cluster_0->mutable_lb_subset_config();
    lb_subset_config->set_fallback_policy(
        envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK);
    auto* subset_selector = lb_subset_config->add_subset_selectors();
    subset_selector->add_keys("role");
    subset_selector->add_keys("version");
    subset_selector->add_keys("stage");
    auto* load_assignment = cluster_0->mutable_load_assignment();
    load_assignment->set_cluster_name("cluster_0");
    auto* locality_lb_endpoints = load_assignment->add_endpoints();
    auto* lb_endpoint = locality_lb_endpoints->add_lb_endpoints();
    lb_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address(
        Network::Test::getLoopbackAddressString(version_));
    lb_endpoint->mutable_metadata()->MergeFrom(endpoint_metadata_);
  });

  TcpProxyIntegrationTest::initialize();
}

// Verifies successful connection.
void TcpProxyMetadataMatchIntegrationTest::expectEndpointToMatchRoute(
    std::function<std::string(IntegrationTcpClient&)> initial_data_cb) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  std::string expected_upstream_data;
  if (initial_data_cb) {
    expected_upstream_data = initial_data_cb(*tcp_client);
  } else {
    expected_upstream_data = "hello";
    ASSERT_TRUE(tcp_client->write(expected_upstream_data));
  }
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(expected_upstream_data.length()));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");
  ASSERT_TRUE(tcp_client->write("hello", true));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5 + expected_upstream_data.length()));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForDisconnect();

  test_server_->waitForCounterGe("cluster.cluster_0.lb_subsets_selected", 1);
}

// Verifies connection failure.
void TcpProxyMetadataMatchIntegrationTest::expectEndpointNotToMatchRoute(
    const std::string& write_data) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write(write_data, false, false));

  // TODO(yskopets): 'tcp_client->waitForDisconnect();' gets stuck indefinitely on Linux builds,
  // e.g. on 'envoy-linux (bazel compile_time_options)' and 'envoy-linux (bazel release)'
  // tcp_client->waitForDisconnect();

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_none_healthy", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.lb_subsets_selected", 0);

  tcp_client->close();
}

INSTANTIATE_TEST_SUITE_P(TcpProxyIntegrationTestParams, TcpProxyMetadataMatchIntegrationTest,
                         testing::ValuesIn(getProtocolTestParams()), protocolTestParamsToString);

// Test subset load balancing for a regular cluster when endpoint selector is defined at the top
// level.
TEST_P(TcpProxyMetadataMatchIntegrationTest,
       EndpointShouldMatchSingleClusterWithTopLevelMetadataMatch) {
  tcp_proxy_.set_stat_prefix("tcp_stats");
  tcp_proxy_.set_cluster("cluster_0");
  tcp_proxy_.mutable_metadata_match()->MergeFrom(
      lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "prod"}}));

  endpoint_metadata_ = lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "prod"}});

  initialize();

  expectEndpointToMatchRoute();
}

// Test subset load balancing for a deprecated_v1 route when endpoint selector is defined at the top
// level.
TEST_P(TcpProxyMetadataMatchIntegrationTest,
       DEPRECATED_FEATURE_TEST(EndpointShouldMatchRouteWithTopLevelMetadataMatch)) {
  tcp_proxy_.set_stat_prefix("tcp_stats");
  tcp_proxy_.set_cluster("fallback");
  tcp_proxy_.mutable_hidden_envoy_deprecated_deprecated_v1()->add_routes()->set_cluster(
      "cluster_0");
  tcp_proxy_.mutable_metadata_match()->MergeFrom(
      lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "prod"}}));

  endpoint_metadata_ = lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "prod"}});

  config_helper_.addRuntimeOverride("envoy.deprecated_features:envoy.extensions.filters.network."
                                    "tcp_proxy.v3.TcpProxy.hidden_envoy_deprecated_deprecated_v1",
                                    "true");
  initialize();

  expectEndpointToMatchRoute();
}

// Test subset load balancing for a weighted cluster when endpoint selector is defined on a weighted
// cluster.
TEST_P(TcpProxyMetadataMatchIntegrationTest, EndpointShouldMatchWeightedClusterWithMetadataMatch) {
  tcp_proxy_.set_stat_prefix("tcp_stats");
  auto* cluster_0 = tcp_proxy_.mutable_weighted_clusters()->add_clusters();
  cluster_0->set_name("cluster_0");
  cluster_0->set_weight(1);
  cluster_0->mutable_metadata_match()->MergeFrom(
      lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "prod"}}));

  endpoint_metadata_ = lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "prod"}});

  initialize();

  expectEndpointToMatchRoute();
}

// Test subset load balancing for a weighted cluster when endpoint selector is defined both on a
// weighted cluster and at the top level.
TEST_P(TcpProxyMetadataMatchIntegrationTest,
       EndpointShouldMatchWeightedClusterWithMetadataMatchAndTopLevelMetadataMatch) {
  tcp_proxy_.set_stat_prefix("tcp_stats");
  tcp_proxy_.mutable_metadata_match()->MergeFrom(lbMetadata({{"version", "v1"}, {"stage", "dev"}}));
  auto* cluster_0 = tcp_proxy_.mutable_weighted_clusters()->add_clusters();
  cluster_0->set_name("cluster_0");
  cluster_0->set_weight(1);
  cluster_0->mutable_metadata_match()->MergeFrom(lbMetadata(
      {{"role", "primary"}, {"stage", "prod"}})); // should override `stage` value at top-level

  endpoint_metadata_ = lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "prod"}});

  initialize();

  expectEndpointToMatchRoute();
}

// Test subset load balancing for a weighted cluster when endpoint selector is defined at the top
// level only.
TEST_P(TcpProxyMetadataMatchIntegrationTest,
       EndpointShouldMatchWeightedClusterWithTopLevelMetadataMatch) {
  tcp_proxy_.set_stat_prefix("tcp_stats");
  tcp_proxy_.mutable_metadata_match()->MergeFrom(
      lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "prod"}}));
  auto* cluster_0 = tcp_proxy_.mutable_weighted_clusters()->add_clusters();
  cluster_0->set_name("cluster_0");
  cluster_0->set_weight(1);

  endpoint_metadata_ = lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "prod"}});

  initialize();

  expectEndpointToMatchRoute();
}

// Test subset load balancing for a regular cluster when endpoint selector is defined at the top
// level.
TEST_P(TcpProxyMetadataMatchIntegrationTest,
       EndpointShouldNotMatchSingleClusterWithTopLevelMetadataMatch) {
  tcp_proxy_.set_stat_prefix("tcp_stats");
  tcp_proxy_.set_cluster("cluster_0");
  tcp_proxy_.mutable_metadata_match()->MergeFrom(
      lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "prod"}}));

  endpoint_metadata_ = lbMetadata({{"role", "replica"}, {"version", "v1"}, {"stage", "prod"}});

  initialize();

  expectEndpointNotToMatchRoute();
}

// Test subset load balancing for a deprecated_v1 route when endpoint selector is defined at the top
// level.
TEST_P(TcpProxyMetadataMatchIntegrationTest,
       DEPRECATED_FEATURE_TEST(EndpointShouldNotMatchRouteWithTopLevelMetadataMatch)) {
  tcp_proxy_.set_stat_prefix("tcp_stats");
  tcp_proxy_.set_cluster("fallback");
  tcp_proxy_.mutable_hidden_envoy_deprecated_deprecated_v1()->add_routes()->set_cluster(
      "cluster_0");
  tcp_proxy_.mutable_metadata_match()->MergeFrom(
      lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "prod"}}));

  endpoint_metadata_ = lbMetadata({{"role", "replica"}, {"version", "v1"}, {"stage", "prod"}});

  config_helper_.addRuntimeOverride("envoy.deprecated_features:envoy.extensions.filters.network."
                                    "tcp_proxy.v3.TcpProxy.hidden_envoy_deprecated_deprecated_v1",
                                    "true");
  initialize();

  expectEndpointNotToMatchRoute();
}

// Test subset load balancing for a weighted cluster when endpoint selector is defined on a weighted
// cluster.
TEST_P(TcpProxyMetadataMatchIntegrationTest,
       EndpointShouldNotMatchWeightedClusterWithMetadataMatch) {
  tcp_proxy_.set_stat_prefix("tcp_stats");
  auto* cluster_0 = tcp_proxy_.mutable_weighted_clusters()->add_clusters();
  cluster_0->set_name("cluster_0");
  cluster_0->set_weight(1);
  cluster_0->mutable_metadata_match()->MergeFrom(
      lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "prod"}}));

  endpoint_metadata_ = lbMetadata({{"role", "replica"}, {"version", "v1"}, {"stage", "prod"}});

  initialize();

  expectEndpointNotToMatchRoute();
}

// Test subset load balancing for a weighted cluster when endpoint selector is defined both on a
// weighted cluster and at the top level.
TEST_P(TcpProxyMetadataMatchIntegrationTest,
       EndpointShouldNotMatchWeightedClusterWithMetadataMatchAndTopLevelMetadataMatch) {
  tcp_proxy_.set_stat_prefix("tcp_stats");
  tcp_proxy_.mutable_metadata_match()->MergeFrom(lbMetadata({{"version", "v1"}, {"stage", "dev"}}));
  auto* cluster_0 = tcp_proxy_.mutable_weighted_clusters()->add_clusters();
  cluster_0->set_name("cluster_0");
  cluster_0->set_weight(1);
  cluster_0->mutable_metadata_match()->MergeFrom(lbMetadata(
      {{"role", "primary"}, {"stage", "prod"}})); // should override `stage` value at top-level

  endpoint_metadata_ = lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "dev"}});

  initialize();

  expectEndpointNotToMatchRoute();
}

// Test subset load balancing for a weighted cluster when endpoint selector is defined at the top
// level only.
TEST_P(TcpProxyMetadataMatchIntegrationTest,
       EndpointShouldNotMatchWeightedClusterWithTopLevelMetadataMatch) {
  tcp_proxy_.set_stat_prefix("tcp_stats");
  tcp_proxy_.mutable_metadata_match()->MergeFrom(
      lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "prod"}}));
  auto* cluster_0 = tcp_proxy_.mutable_weighted_clusters()->add_clusters();
  cluster_0->set_name("cluster_0");
  cluster_0->set_weight(1);

  endpoint_metadata_ = lbMetadata({{"role", "replica"}, {"version", "v1"}, {"stage", "prod"}});

  initialize();

  expectEndpointNotToMatchRoute();
}

class InjectDynamicMetadata : public Network::ReadFilter {
public:
  explicit InjectDynamicMetadata(const std::string& key) : key_(key) {}

  Network::FilterStatus onData(Buffer::Instance& data, bool) override {
    if (!metadata_set_) {
      // To allow testing a write that returns `StopIteration`, only proceed
      // when more than 1 byte is received.
      if (data.length() < 2) {
        ASSERT(data.length() == 1);

        // Echo data back to test can verify it was received.
        Buffer::OwnedImpl copy(data);
        read_callbacks_->connection().write(copy, false);
        return Network::FilterStatus::StopIteration;
      }

      ProtobufWkt::Value val;
      val.set_string_value(data.toString());

      ProtobufWkt::Struct& map =
          (*read_callbacks_->connection()
                .streamInfo()
                .dynamicMetadata()
                .mutable_filter_metadata())[Envoy::Config::MetadataFilters::get().ENVOY_LB];
      (*map.mutable_fields())[key_] = val;

      // Put this back in the state that TcpProxy expects.
      read_callbacks_->connection().readDisable(true);

      metadata_set_ = true;
    }
    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onNewConnection() override {
    // TcpProxy disables read; must re-enable so we can read headers.
    read_callbacks_->connection().readDisable(false);

    // Stop until we read the value and can set the metadata for TcpProxy.
    return Network::FilterStatus::StopIteration;
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  const std::string key_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  bool metadata_set_{false};
};

class InjectDynamicMetadataFactory : public Extensions::NetworkFilters::Common::FactoryBase<
                                         test::integration::tcp_proxy::InjectDynamicMetadata> {
public:
  InjectDynamicMetadataFactory() : FactoryBase("test.inject_dynamic_metadata") {}

private:
  Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const test::integration::tcp_proxy::InjectDynamicMetadata& cfg,
                                    Server::Configuration::FactoryContext&) override {
    std::string key = cfg.key();
    return [key = std::move(key)](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<InjectDynamicMetadata>(key));
    };
  }
};

class TcpProxyDynamicMetadataMatchIntegrationTest : public TcpProxyMetadataMatchIntegrationTest {
public:
  TcpProxyDynamicMetadataMatchIntegrationTest() : TcpProxyMetadataMatchIntegrationTest(1) {
    config_helper_.addNetworkFilter(R"EOF(
      name: test.inject_dynamic_metadata
      typed_config:
        "@type": type.googleapis.com/test.integration.tcp_proxy.InjectDynamicMetadata
        key: role
)EOF");
  }

  InjectDynamicMetadataFactory factory_;
  Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory> register_factory_{
      factory_};
};

INSTANTIATE_TEST_SUITE_P(TcpProxyIntegrationTestParams, TcpProxyDynamicMetadataMatchIntegrationTest,
                         testing::ValuesIn(getProtocolTestParams()), protocolTestParamsToString);

TEST_P(TcpProxyDynamicMetadataMatchIntegrationTest, DynamicMetadataMatch) {
  tcp_proxy_.set_stat_prefix("tcp_stats");

  // Note: role isn't set here; it will be set in the dynamic metadata.
  tcp_proxy_.mutable_metadata_match()->MergeFrom(
      lbMetadata({{"version", "v1"}, {"stage", "prod"}}));
  auto* cluster_0 = tcp_proxy_.mutable_weighted_clusters()->add_clusters();
  cluster_0->set_name("cluster_0");
  cluster_0->set_weight(1);
  endpoint_metadata_ = lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "prod"}});

  initialize();

  expectEndpointToMatchRoute([](IntegrationTcpClient& tcp_client) -> std::string {
    // Break the write into two; validate that the first is received before sending the second. This
    // validates that a downstream filter can use this functionality, even if it can't make a
    // decision after the first `onData()`.
    EXPECT_TRUE(tcp_client.write("p", false));
    tcp_client.waitForData("p");
    tcp_client.clearData();
    EXPECT_TRUE(tcp_client.write("rimary", false));
    return "primary";
  });
}

TEST_P(TcpProxyDynamicMetadataMatchIntegrationTest, DynamicMetadataNonMatch) {
  tcp_proxy_.set_stat_prefix("tcp_stats");

  // Note: role isn't set here; it will be set in the dynamic metadata.
  tcp_proxy_.mutable_metadata_match()->MergeFrom(
      lbMetadata({{"version", "v1"}, {"stage", "prod"}}));
  auto* cluster_0 = tcp_proxy_.mutable_weighted_clusters()->add_clusters();
  cluster_0->set_name("cluster_0");
  cluster_0->set_weight(1);
  endpoint_metadata_ = lbMetadata({{"role", "primary"}, {"version", "v1"}, {"stage", "prod"}});

  initialize();

  expectEndpointNotToMatchRoute("does_not_match_role_primary");
}

INSTANTIATE_TEST_SUITE_P(TcpProxyIntegrationTestParams, TcpProxySslIntegrationTest,
                         testing::ValuesIn(getProtocolTestParams()), protocolTestParamsToString);

void TcpProxySslIntegrationTest::initialize() {
  config_helper_.addSslConfig();
  TcpProxyIntegrationTest::initialize();

  context_manager_ =
      std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());
  payload_reader_ = std::make_shared<WaitForPayloadReader>(*dispatcher_);
}

void TcpProxySslIntegrationTest::setupConnections() {
  initialize();

  // Set up the mock buffer factory so the newly created SSL client will have a mock write
  // buffer. This allows us to track the bytes actually written to the socket.

  EXPECT_CALL(*mock_buffer_factory_, create_(_, _, _))
      .Times(1)
      .WillOnce(Invoke([&](std::function<void()> below_low, std::function<void()> above_high,
                           std::function<void()> above_overflow) -> Buffer::Instance* {
        client_write_buffer_ =
            new NiceMock<MockWatermarkBuffer>(below_low, above_high, above_overflow);
        ON_CALL(*client_write_buffer_, move(_))
            .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove));
        ON_CALL(*client_write_buffer_, drain(_))
            .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::trackDrains));
        return client_write_buffer_;
      }));
  // Set up the SSL client.
  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("tcp_proxy"));
  context_ = Ssl::createClientSslTransportSocketFactory({}, *context_manager_, *api_);
  ssl_client_ =
      dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                          context_->createTransportSocket(nullptr), nullptr);

  // Perform the SSL handshake. Loopback is allowlisted in tcp_proxy.json for the ssl_auth
  // filter so there will be no pause waiting on auth data.
  ssl_client_->addConnectionCallbacks(connect_callbacks_);
  ssl_client_->enableHalfClose(true);
  ssl_client_->addReadFilter(payload_reader_);
  ssl_client_->connect();
  while (!connect_callbacks_.connected()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  AssertionResult result = fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_);
  RELEASE_ASSERT(result, result.message());
}

// Test proxying data in both directions with envoy doing TCP and TLS
// termination.
void TcpProxySslIntegrationTest::sendAndReceiveTlsData(const std::string& data_to_send_upstream,
                                                       const std::string& data_to_send_downstream) {
  // Ship some data upstream.
  Buffer::OwnedImpl buffer(data_to_send_upstream);
  ssl_client_->write(buffer, false);
  while (client_write_buffer_->bytesDrained() != data_to_send_upstream.size()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection_->waitForData(data_to_send_upstream.size()));

  // Now send data downstream and make sure it arrives.
  ASSERT_TRUE(fake_upstream_connection_->write(data_to_send_downstream));
  payload_reader_->set_data_to_wait_for(data_to_send_downstream);
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);

  // Clean up.
  Buffer::OwnedImpl empty_buffer;
  ssl_client_->write(empty_buffer, true);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  ASSERT_TRUE(fake_upstream_connection_->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection_->write("", true));
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(payload_reader_->readLastByte());
  EXPECT_TRUE(connect_callbacks_.closed());
}

TEST_P(TcpProxySslIntegrationTest, SendTlsToTlsListener) {
  setupConnections();
  sendAndReceiveTlsData("hello", "world");
}

TEST_P(TcpProxySslIntegrationTest, LargeBidirectionalTlsWrites) {
  setupConnections();
  std::string large_data(1024 * 8, 'a');
  sendAndReceiveTlsData(large_data, large_data);
}

// Test that a half-close on the downstream side is proxied correctly.
TEST_P(TcpProxySslIntegrationTest, DownstreamHalfClose) {
  setupConnections();

  Buffer::OwnedImpl empty_buffer;
  ssl_client_->write(empty_buffer, true);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  ASSERT_TRUE(fake_upstream_connection_->waitForHalfClose());

  const std::string data("data");
  ASSERT_TRUE(fake_upstream_connection_->write(data, false));
  payload_reader_->set_data_to_wait_for(data);
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_FALSE(payload_reader_->readLastByte());

  ASSERT_TRUE(fake_upstream_connection_->write("", true));
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(payload_reader_->readLastByte());
}

// Test that a half-close on the upstream side is proxied correctly.
TEST_P(TcpProxySslIntegrationTest, UpstreamHalfClose) {
  setupConnections();

  ASSERT_TRUE(fake_upstream_connection_->write("", true));
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(payload_reader_->readLastByte());
  EXPECT_FALSE(connect_callbacks_.closed());

  const std::string& val("data");
  Buffer::OwnedImpl buffer(val);
  ssl_client_->write(buffer, false);
  while (client_write_buffer_->bytesDrained() != val.size()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  ASSERT_TRUE(fake_upstream_connection_->waitForData(val.size()));

  Buffer::OwnedImpl empty_buffer;
  ssl_client_->write(empty_buffer, true);
  while (!connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  ASSERT_TRUE(fake_upstream_connection_->waitForHalfClose());
}

} // namespace Envoy
