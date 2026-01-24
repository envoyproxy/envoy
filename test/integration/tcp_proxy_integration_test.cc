#include <memory>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "source/common/config/api_version.h"
#include "source/common/network/utility.h"
#include "source/common/stream_info/bool_accessor_impl.h"
#include "source/common/tcp_proxy/tcp_proxy.h"
#include "source/common/tls/context_manager_impl.h"
#include "source/extensions/filters/network/common/factory_base.h"

#include "test/integration/fake_access_log.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/tcp_proxy_integration.h"
#include "test/integration/tcp_proxy_integration_test.pb.h"
#include "test/integration/tcp_proxy_integration_test.pb.validate.h"
#include "test/integration/utility.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::Invoke;
using testing::MatchesRegex;
using testing::NiceMock;

namespace Envoy {

INSTANTIATE_TEST_SUITE_P(IpVersions, TcpProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test upstream writing before downstream does.
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
  // Any time an associated connection is destroyed, it increments both counters.
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_destroy", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_destroy_with_active_rq", 1);

  IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection2));
  tcp_client2->close();
}

// Test TLS upstream.
TEST_P(TcpProxyIntegrationTest, TcpProxyUpstreamTls) {
  upstream_tls_ = true;
  setUpstreamProtocol(Http::CodecType::HTTP1);
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
TEST_P(TcpProxyIntegrationTest, TcpProxyUpstreamDisconnectBytesMeter) {
  setupByteMeterAccessLog();
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
  test_server_.reset();
  auto log_result = waitForAccessLog(listener_access_log_name_);
  EXPECT_THAT(log_result, MatchesRegex(fmt::format("DOWNSTREAM_WIRE_BYTES_SENT=5 "
                                                   "DOWNSTREAM_WIRE_BYTES_RECEIVED=5 "
                                                   "UPSTREAM_WIRE_BYTES_SENT=5 "
                                                   "UPSTREAM_WIRE_BYTES_RECEIVED=5"
                                                   "\r?.*")));
}

// Test proxying data in both directions, and that all data is flushed properly
// when the client disconnects.
TEST_P(TcpProxyIntegrationTest, TcpProxyDownstreamDisconnectBytesMeter) {
  setupByteMeterAccessLog();
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
  test_server_.reset();
  auto log_result = waitForAccessLog(listener_access_log_name_);
  EXPECT_THAT(log_result, MatchesRegex(fmt::format("DOWNSTREAM_WIRE_BYTES_SENT=5 "
                                                   "DOWNSTREAM_WIRE_BYTES_RECEIVED=10 "
                                                   "UPSTREAM_WIRE_BYTES_SENT=10 "
                                                   "UPSTREAM_WIRE_BYTES_RECEIVED=5"
                                                   "\r?.*")));
}

TEST_P(TcpProxyIntegrationTest, TcpProxyRandomBehavior) {
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
  enableHalfClose(false);
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
      test_server_->counter("tcp.tcpproxy_stats.downstream_flow_control_paused_reading_total")
          ->value();
  uint32_t downstream_resumes =
      test_server_->counter("tcp.tcpproxy_stats.downstream_flow_control_resumed_reading_total")
          ->value();
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

  test_server_->waitForGaugeEq("tcp.tcpproxy_stats.upstream_flush_active", 1);
  ASSERT_TRUE(fake_upstream_connection->readDisable(false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(data.size()));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForHalfClose();

  EXPECT_EQ(test_server_->counter("tcp.tcpproxy_stats.upstream_flush_total")->value(), 1);
  test_server_->waitForGaugeEq("tcp.tcpproxy_stats.upstream_flush_active", 0);
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

  test_server_->waitForGaugeEq("tcp.tcpproxy_stats.upstream_flush_active", 1);
  test_server_.reset();
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  // Success criteria is that no ASSERTs fire and there are no leaks.
}

// Check for basic log access.
// Tests the values of %UPSTREAM_LOCAL_ADDRESS%, %UPSTREAM_HOST%,
//  %DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%, %BYTES_SENT%, %BYTES_RECEIVED%,
//  %DOWNSTREAM_WIRE_BYTES_SENT%, %DOWNSTREAM_WIRE_BYTES_RECEIVED%,
//  %UPSTREAM_WIRE_BYTES_SENT%, %UPSTREAM_WIRE_BYTES_RECEIVED%
TEST_P(TcpProxyIntegrationTest, AccessLogBytesMeter) {
  std::string access_log_path = TestEnvironment::temporaryPath(
      fmt::format("access_log{}{}.txt", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6",
                  TestUtility::uniqueFilename()));
  useListenerAccessLog();
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
        "upstreamhost=%UPSTREAM_HOST% "
        "downstream=%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT% "
        "sent=%BYTES_SENT% received=%BYTES_RECEIVED% "
        "DOWNSTREAM_WIRE_BYTES_SENT=%DOWNSTREAM_WIRE_BYTES_SENT% "
        "DOWNSTREAM_WIRE_BYTES_RECEIVED=%DOWNSTREAM_WIRE_BYTES_RECEIVED% "
        "UPSTREAM_WIRE_BYTES_SENT=%UPSTREAM_WIRE_BYTES_SENT% "
        "UPSTREAM_WIRE_BYTES_RECEIVED=%UPSTREAM_WIRE_BYTES_RECEIVED%");
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

  // Guarantee client is done writing to the log.
  test_server_.reset();
  auto log_result = waitForAccessLog(access_log_path);

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
              MatchesRegex(fmt::format("upstreamlocal={0} upstreamhost={0} downstream={1} "
                                       "sent=5 received=0 "
                                       "DOWNSTREAM_WIRE_BYTES_SENT=5 "
                                       "DOWNSTREAM_WIRE_BYTES_RECEIVED=0 "
                                       "UPSTREAM_WIRE_BYTES_SENT=0 "
                                       "UPSTREAM_WIRE_BYTES_RECEIVED=5"
                                       "\r?.*",
                                       ip_port_regex, ip_regex)));
}

// Verifies that access log value for `UPSTREAM_TRANSPORT_FAILURE_REASON` matches the failure
// message when there is an upstream transport failure.
TEST_P(TcpProxyIntegrationTest, AccessLogUpstreamConnectFailure) {
  std::string access_log_path = TestEnvironment::temporaryPath(
      fmt::format("access_log{}{}.txt", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6",
                  TestUtility::uniqueFilename()));
  useListenerAccessLog();
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
        "%UPSTREAM_TRANSPORT_FAILURE_REASON%");
    access_log->mutable_typed_config()->PackFrom(access_log_config);
    config_blob->PackFrom(tcp_proxy_config);
  });

  // Ensure we don't get an upstream connection.
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

  // Guarantee client is done writing to the log.
  auto log_result = waitForAccessLog(access_log_path);

  EXPECT_THAT(log_result, testing::StartsWith("delayed_connect_error:"));
}

// Verifies that access log value for `DOWNSTREAM_LOCAL_CLOSE_REASON` matches
// the failure message when there is a session idle timeout.
TEST_P(TcpProxyIntegrationTest, AccessLogSessionIdleTimeout) {
  std::string access_log_path = TestEnvironment::temporaryPath(
      fmt::format("access_log{}{}.txt", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6",
                  TestUtility::uniqueFilename()));
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
        "%DOWNSTREAM_LOCAL_CLOSE_REASON%");
    access_log->mutable_typed_config()->PackFrom(access_log_config);
    tcp_proxy_config.mutable_idle_timeout()->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(500))
            .count());
    config_blob->PackFrom(tcp_proxy_config);
  });
  enableHalfClose(false);
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  // Session should idle timeout, causing disconnect.
  tcp_client->waitForDisconnect();
  // Guarantee client is done writing to the log.
  auto log_result = waitForAccessLog(access_log_path);
  EXPECT_EQ(log_result, "tcp_session_idle_timeout");
}

// Verifies that access log value for `UPSTREAM_DETECTED_CLOSE_TYPE` matches the
// upstream close type.
TEST_P(TcpProxyIntegrationTest, AccessLogUpstreamDetectedCloseType) {
  std::string access_log_path = TestEnvironment::temporaryPath(
      fmt::format("access_log{}{}.txt", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6",
                  TestUtility::uniqueFilename()));
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
        "%UPSTREAM_DETECTED_CLOSE_TYPE%");
    access_log->mutable_typed_config()->PackFrom(access_log_config);
    config_blob->PackFrom(tcp_proxy_config);
  });
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // Close the upstream connection.
  ASSERT_TRUE(fake_upstream_connection->close(Network::ConnectionCloseType::AbortReset));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  // Wait for the upstream to close to ensure we get the correct close type.
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_destroy_remote", 1,
                                 TestUtility::DefaultTimeout * 100);

  // Downstream should be closed by proxy.
  tcp_client->close();

  // Guarantee client is done writing to the log.
  auto log_result = waitForAccessLog(access_log_path);
  EXPECT_THAT(log_result, testing::Eq("RemoteReset"));
}

TEST_P(TcpProxyIntegrationTest, AccessLogOnUpstreamConnect) {
  std::string access_log_path = TestEnvironment::temporaryPath(
      fmt::format("access_log{}{}.txt", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6",
                  TestUtility::uniqueFilename()));

  setupByteMeterAccessLog();
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>());
    auto tcp_proxy_config =
        MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
            *config_blob);

    tcp_proxy_config.mutable_access_log_options()->set_flush_access_log_on_connected(true);
    auto* access_log = tcp_proxy_config.add_access_log();
    access_log->set_name("accesslog");
    envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
    access_log_config.set_path(access_log_path);
    access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        "%ACCESS_LOG_TYPE%-%UPSTREAM_CONNECTION_ID%\n");
    access_log->mutable_typed_config()->PackFrom(access_log_config);
    config_blob->PackFrom(tcp_proxy_config);
  });

  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr fake_upstream_connection;

  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  auto log_result = waitForAccessLog(access_log_path);
  std::vector<std::string> access_log_parts = absl::StrSplit(log_result, '-');
  EXPECT_EQ(AccessLogType_Name(AccessLog::AccessLogType::TcpUpstreamConnected),
            access_log_parts[0]);
  uint32_t upstream_connection_id;
  ASSERT_TRUE(absl::SimpleAtoi(access_log_parts[1], &upstream_connection_id));
  EXPECT_GT(upstream_connection_id, 0);

  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForDisconnect();
  test_server_.reset();

  log_result = waitForAccessLog(access_log_path, 1);
  access_log_parts = absl::StrSplit(log_result, '-');
  EXPECT_EQ(AccessLogType_Name(AccessLog::AccessLogType::TcpConnectionEnd), access_log_parts[0]);
  ASSERT_TRUE(absl::SimpleAtoi(access_log_parts[1], &upstream_connection_id));
  EXPECT_GT(upstream_connection_id, 0);
}

TEST_P(TcpProxyIntegrationTest, PeriodicAccessLog) {
  std::string access_log_path = TestEnvironment::temporaryPath(
      fmt::format("access_log{}{}.txt", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6",
                  TestUtility::uniqueFilename()));

  setupByteMeterAccessLog();
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>());
    auto tcp_proxy_config =
        MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
            *config_blob);

    tcp_proxy_config.mutable_access_log_options()->mutable_access_log_flush_interval()->set_nanos(
        100000000); // 0.1 seconds
    auto* access_log = tcp_proxy_config.add_access_log();
    access_log->set_name("accesslog");
    envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
    access_log_config.set_path(access_log_path);
    access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        "ACCESS_LOG_TYPE=%ACCESS_LOG_TYPE%");
    access_log->mutable_typed_config()->PackFrom(access_log_config);
    config_blob->PackFrom(tcp_proxy_config);
  });

  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr fake_upstream_connection;

  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  auto log_result = waitForAccessLog(access_log_path);
  EXPECT_EQ(
      absl::StrCat("ACCESS_LOG_TYPE=", AccessLogType_Name(AccessLog::AccessLogType::TcpPeriodic)),
      log_result);

  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForDisconnect();
  test_server_.reset();
}

// Make sure no bytes are logged when no data is sent.
TEST_P(TcpProxyIntegrationTest, TcpProxyNoDataBytesMeter) {
  setupByteMeterAccessLog();
  initialize();

  FakeRawConnectionPtr fake_upstream_connection;
  auto tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->close());

  test_server_.reset();
  auto log_result = waitForAccessLog(listener_access_log_name_);
  EXPECT_THAT(log_result, MatchesRegex(fmt::format("DOWNSTREAM_WIRE_BYTES_SENT=0 "
                                                   "DOWNSTREAM_WIRE_BYTES_RECEIVED=0 "
                                                   "UPSTREAM_WIRE_BYTES_SENT=0 "
                                                   "UPSTREAM_WIRE_BYTES_RECEIVED=0"
                                                   "\r?.*")));
}

// Test Byte Metering across multiple upstream/downstream connections
TEST_P(TcpProxyIntegrationTest, TcpProxyBidirectionalBytesMeter) {
  setupByteMeterAccessLog();
  const int client_count = 3;
  const int upstream_count = 3;

  enableHalfClose(false);
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto* load_assignment = cluster->mutable_load_assignment();
    load_assignment->clear_endpoints();
    for (int i = 0; i < upstream_count; ++i) {
      auto locality = load_assignment->add_endpoints();
      locality->add_lb_endpoints()->mutable_endpoint()->MergeFrom(
          ConfigHelper::buildEndpoint(Network::Test::getLoopbackAddressString(version_)));
    }
  });

  setUpstreamCount(upstream_count);
  initialize();

  std::vector<IntegrationTcpClientPtr> clients{client_count};
  std::vector<FakeRawConnectionPtr> fake_connections{upstream_count};
  const auto indices = std::vector<uint64_t>({0, 1, 2});

  for (int i = 0; i < client_count; ++i) {
    clients[i] = makeTcpConnection(lookupPort("tcp_proxy"));
    waitForNextRawUpstreamConnection(indices, fake_connections[i]);
  }

  ASSERT_TRUE(clients[0]->write("hello")); // send initial client data
  ASSERT_TRUE(fake_connections[0]->waitForData(5));

  ASSERT_TRUE(fake_connections[0]->write("squack")); // send upstream data
  ASSERT_TRUE(clients[0]->waitForData(6));

  ASSERT_TRUE(clients[0]->write("hey")); // send more client data
  ASSERT_TRUE(fake_connections[0]->waitForData(8));

  ASSERT_TRUE(fake_connections[2]->write("bye")); // send data from 3rd client
  ASSERT_TRUE(clients[2]->waitForData(3));

  for (int i = 0; i < client_count; ++i) {
    ASSERT_TRUE(fake_connections[i]->close());
    clients[i]->close();
    ASSERT_TRUE(fake_connections[i]->waitForDisconnect());
  }
  test_server_.reset();

  std::vector<absl::string_view> logs = {
      "DOWNSTREAM_WIRE_BYTES_SENT=6 DOWNSTREAM_WIRE_BYTES_RECEIVED=8 "
      "UPSTREAM_WIRE_BYTES_SENT=8 UPSTREAM_WIRE_BYTES_RECEIVED=6",
      "DOWNSTREAM_WIRE_BYTES_SENT=0 DOWNSTREAM_WIRE_BYTES_RECEIVED=0 "
      "UPSTREAM_WIRE_BYTES_SENT=0 UPSTREAM_WIRE_BYTES_RECEIVED=0",
      "DOWNSTREAM_WIRE_BYTES_SENT=3 DOWNSTREAM_WIRE_BYTES_RECEIVED=0 "
      "UPSTREAM_WIRE_BYTES_SENT=0 UPSTREAM_WIRE_BYTES_RECEIVED=3"};
  auto log_result = waitForAccessLog(listener_access_log_name_);
  for (int i = 0; i < client_count; ++i) {
    EXPECT_THAT(log_result, MatchesRegex(fmt::format(".*{}.*", logs[i])));
  }
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

TEST_P(TcpProxyIntegrationTest, TestPerClientIdletimeout) {
  autonomous_upstream_ = true;

  enableHalfClose(false);
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* static_resources = bootstrap.mutable_static_resources();
    for (int i = 0; i < static_resources->clusters_size(); ++i) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(i);
      auto& cluster_protocol_options = *cluster->mutable_typed_extension_protocol_options();
      envoy::extensions::upstreams::tcp::v3::TcpProtocolOptions tcp_options;
      tcp_options.mutable_idle_timeout()->set_nanos(
          std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(100))
              .count());
      cluster_protocol_options["envoy.extensions.upstreams.tcp.v3.TcpProtocolOptions"].PackFrom(
          tcp_options);

      // two more connections which are going to be closed by the per-client idle timers
      cluster->mutable_preconnect_policy()->mutable_predictive_preconnect_ratio()->set_value(2);

      auto* load_assignment = cluster->mutable_load_assignment();
      load_assignment->clear_endpoints();
      for (int i = 0; i < 5; ++i) {
        auto locality = load_assignment->add_endpoints();
        locality->add_lb_endpoints()->mutable_endpoint()->MergeFrom(
            ConfigHelper::buildEndpoint(Network::Test::getLoopbackAddressString(version_)));
      }
    }
  });
  setUpstreamCount(5);

  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  // Platforms could prevent ActiveTcpClient construction unless we explicitly wait for it.
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);

  tcp_client->close();

  // Two pre-connections are closed by idle timers.
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_destroy", 2);
}

TEST_P(TcpProxyIntegrationTest, TestIdletimeoutWithLargeOutstandingData) {
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

TEST_P(TcpProxyIntegrationTest, TestMaxDownstreamConnectionDurationWithNoData) {
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

TEST_P(TcpProxyIntegrationTest, TestMaxDownstreamConnectionDurationWithLargeOutstandingData) {
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

TEST_P(TcpProxyIntegrationTest, TestMaxDownstreamConnectionDurationWithJitter) {
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
    tcp_proxy_config.mutable_max_downstream_connection_duration_jitter_percentage()->set_value(25);
    config_blob->PackFrom(tcp_proxy_config);
  });

  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  tcp_client->waitForDisconnect();
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
}

TEST_P(TcpProxyIntegrationTest, RecordsUpstreamConnectionTimeLatency) {
  FakeAccessLogFactory factory;
  factory.setLogCallback([](const Formatter::Context&, const StreamInfo::StreamInfo& stream_info) {
    EXPECT_TRUE(
        stream_info.upstreamInfo()->upstreamTiming().connectionPoolCallbackLatency().has_value());
  });

  Registry::InjectFactory<AccessLog::AccessLogInstanceFactory> factory_register(factory);

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>());
    auto tcp_proxy_config =
        MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
            *config_blob);

    auto* access_log = tcp_proxy_config.add_access_log();
    access_log->set_name("testaccesslog");
    test::integration::accesslog::FakeAccessLog access_log_config;
    access_log->mutable_typed_config()->PackFrom(access_log_config);
    config_blob->PackFrom(tcp_proxy_config);
  });

  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("ping", true));
  ASSERT_TRUE(fake_upstream_connection->write("pong", true));

  tcp_client->waitForHalfClose();
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
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

  Protobuf::Struct map;
  auto* mutable_fields = map.mutable_fields();
  Protobuf::Value value;

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
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

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

      Protobuf::Value val;
      val.set_string_value(data.toString());

      Protobuf::Struct& map =
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
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

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
    // validates that a downstream network filter can use this functionality, even if it can't make
    // a decision after the first `onData()`.
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
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(TcpProxySslIntegrationTest, SendTlsToTlsListener) {
  setupConnections();
  sendAndReceiveTlsData("hello", "world");
}

TEST_P(TcpProxySslIntegrationTest, LargeBidirectionalTlsWrites) {
  setupConnections();
  std::string large_data(1024 * 8, 'a');
  sendAndReceiveTlsData(large_data, large_data);
}

// Test that if SSL connection data, such as peer certificate data, is read before it is
// available, it is not cached when it is read again later when available.
TEST_P(TcpProxySslIntegrationTest, SslConnectionDataEarlyReadNotCached) {
  std::string access_log_path = TestEnvironment::temporaryPath(
      fmt::format("access_log{}{}.txt", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6",
                  TestUtility::uniqueFilename()));
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
        "san=%DOWNSTREAM_PEER_URI_SAN% fingerprint=%DOWNSTREAM_PEER_FINGERPRINT_256%\n");
    access_log->mutable_typed_config()->PackFrom(access_log_config);
    tcp_proxy_config.mutable_access_log_options()->set_flush_access_log_on_connected(true);
    config_blob->PackFrom(tcp_proxy_config);
  });

  setupConnections();
  std::string large_data(1024 * 8, 'a');
  sendAndReceiveTlsData(large_data, large_data);

  // The test set `flush_access_log_on_connected`, so the first access log is emitted before the
  // handshake has completed.
  auto log_result = waitForAccessLog(access_log_path, 0, true);
  EXPECT_EQ(log_result, "san=- fingerprint=-");

  // The second access log is when the connection closes, so the handshake is complete and
  // a valid peer cert is now available.
  log_result = waitForAccessLog(access_log_path, 1, false);
  EXPECT_EQ(log_result,
            "san=spiffe://lyft.com/frontend-team,http://frontend.lyft.com "
            "fingerprint=7346b3836cfc41385351191b5e6163f1a69704cfdf0a03634ed2019128e6fdc4");
}

// Test that a half-close on the downstream side is proxied correctly.
TEST_P(TcpProxySslIntegrationTest, DownstreamHalfClose) {
  setupConnections();

  Buffer::OwnedImpl empty_buffer;
  client_->ssl_client_->write(empty_buffer, true);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  ASSERT_TRUE(client_->fake_upstream_connection_->waitForHalfClose());

  const std::string data("data");
  ASSERT_TRUE(client_->fake_upstream_connection_->write(data, false));
  client_->payload_reader_->setDataToWaitFor(data);
  client_->ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_FALSE(client_->payload_reader_->readLastByte());

  ASSERT_TRUE(client_->fake_upstream_connection_->write("", true));
  client_->ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(client_->payload_reader_->readLastByte());
}

// Test that a half-close on the upstream side is proxied correctly.
TEST_P(TcpProxySslIntegrationTest, UpstreamHalfClose) {
  setupConnections();

  ASSERT_TRUE(client_->fake_upstream_connection_->write("", true));
  client_->ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(client_->payload_reader_->readLastByte());
  EXPECT_FALSE(client_->connect_callbacks_.closed());

  const std::string& val("data");
  Buffer::OwnedImpl buffer(val);
  client_->ssl_client_->write(buffer, false);
  while (client_->client_write_buffer_->bytesDrained() != val.size()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  ASSERT_TRUE(client_->fake_upstream_connection_->waitForData(val.size()));

  Buffer::OwnedImpl empty_buffer;
  client_->ssl_client_->write(empty_buffer, true);
  while (!client_->connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  ASSERT_TRUE(client_->fake_upstream_connection_->waitForHalfClose());
}

// Integration test a Mysql upstream, where the upstream sends data immediately
// after a connection is established.
class FakeMysqlUpstream : public FakeUpstream {
  using FakeUpstream::FakeUpstream;

  bool createNetworkFilterChain(Network::Connection& connection,
                                const Filter::NetworkFilterFactoriesList& cb) override {
    Buffer::OwnedImpl to_write("P");
    connection.write(to_write, false);
    return FakeUpstream::createNetworkFilterChain(connection, cb);
  }
};

class MysqlIntegrationTest : public TcpProxyIntegrationTest {
public:
  void createUpstreams() override {
    for (uint32_t i = 0; i < fake_upstreams_count_; ++i) {
      Network::DownstreamTransportSocketFactoryPtr factory =
          upstream_tls_ ? createUpstreamTlsContext(upstreamConfig())
                        : Network::Test::createRawBufferDownstreamSocketFactory();
      auto endpoint = upstream_address_fn_(i);
      fake_upstreams_.emplace_back(
          new FakeMysqlUpstream(std::move(factory), endpoint, upstreamConfig()));
    }
  }

  void globalPreconnect() {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          bootstrap.mutable_static_resources()
              ->mutable_clusters(0)
              ->mutable_preconnect_policy()
              ->mutable_predictive_preconnect_ratio()
              ->set_value(1.5);
        });
  }

  void perUpstreamPreconnect() {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          bootstrap.mutable_static_resources()
              ->mutable_clusters(0)
              ->mutable_preconnect_policy()
              ->mutable_per_upstream_preconnect_ratio()
              ->set_value(1.5);
        });
  }

  void testPreconnect();
};

// This just verifies that FakeMysqlUpstream works as advertised, and the early data
// makes it to the client.
TEST_P(MysqlIntegrationTest, UpstreamWritesFirst) {
  globalPreconnect();
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  tcp_client->waitForData("P", false);

  ASSERT_TRUE(tcp_client->write("F"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(1));

  ASSERT_TRUE(fake_upstream_connection->write("", true));
  tcp_client->waitForHalfClose();
  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

// Make sure that with the connection read disabled, that disconnect detection
// works.
// Early close notification does not work for OSX
#if !defined(__APPLE__)
TEST_P(MysqlIntegrationTest, DisconnectDetected) {
  // Switch to per-upstream preconnect.
  perUpstreamPreconnect();
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  FakeRawConnectionPtr fake_upstream_connection1;
  // The needed connection.
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  // The prefetched connection.
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection1));

  // Close the prefetched connection.
  ASSERT_TRUE(fake_upstream_connection1->close());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_destroy", 1);

  tcp_client->close();
}
#endif

void MysqlIntegrationTest::testPreconnect() {
  globalPreconnect();
  enableHalfClose(false);
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto* load_assignment = cluster->mutable_load_assignment();
    load_assignment->clear_endpoints();
    for (int i = 0; i < 5; ++i) {
      auto locality = load_assignment->add_endpoints();
      locality->add_lb_endpoints()->mutable_endpoint()->MergeFrom(
          ConfigHelper::buildEndpoint(Network::Test::getLoopbackAddressString(version_)));
    }
  });

  setUpstreamCount(5);
  initialize();
  int num_clients = 10;

  std::vector<IntegrationTcpClientPtr> clients{10};
  std::vector<FakeRawConnectionPtr> fake_connections{15};

  int upstream_index = 0;
  for (int i = 0; i < num_clients; ++i) {
    // Start a new request.
    clients[i] = makeTcpConnection(lookupPort("tcp_proxy"));
    waitForNextRawUpstreamConnection(std::vector<uint64_t>({0, 1, 2, 3, 4}),
                                     fake_connections[upstream_index]);
    ++upstream_index;

    // For every other connection, an extra connection should be preconnected.
    if (i % 2 == 0) {
      waitForNextRawUpstreamConnection(std::vector<uint64_t>({0, 1, 2, 3, 4}),
                                       fake_connections[upstream_index]);
      ++upstream_index;
    }
  }
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_destroy")->value());
  // Clean up.
  for (int i = 0; i < num_clients; ++i) {
    clients[i]->close();
  }
}

TEST_P(MysqlIntegrationTest, Preconnect) { testPreconnect(); }

TEST_P(MysqlIntegrationTest, PreconnectWithTls) {
  upstream_tls_ = true;
  setUpstreamProtocol(Http::CodecType::HTTP1);
  config_helper_.configureUpstreamTls();
  testPreconnect();
}

INSTANTIATE_TEST_SUITE_P(TcpProxyIntegrationTestParams, MysqlIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

class PauseFilter : public Network::ReadFilter {
public:
  explicit PauseFilter(int data_size_before_continue)
      : data_size_before_continue_(data_size_before_continue) {}

  Network::FilterStatus onData(Buffer::Instance& buffer, bool) override {
    // Once the initial buffer size is reached, the filter chain will be continued.
    should_continue_ = should_continue_ || buffer.length() >= data_size_before_continue_;
    return should_continue_ ? Network::FilterStatus::Continue
                            : Network::FilterStatus::StopIteration;
  }

  Network::FilterStatus onNewConnection() override {
    // Stop Iteration as more data is needed before filter chain can be continued.
    return Network::FilterStatus::StopIteration;
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;

    // Pass ReceiveBeforeConnect state to TCP_PROXY so that it does not read disable
    // the connection.
    read_callbacks_->connection().streamInfo().filterState()->setData(
        TcpProxy::ReceiveBeforeConnectKey, std::make_unique<StreamInfo::BoolAccessorImpl>(true),
        StreamInfo::FilterState::StateType::ReadOnly,
        StreamInfo::FilterState::LifeSpan::Connection);
  }

  // Whether the filter chain should be continued.
  bool should_continue_{false};
  // The number of bytes to receive before the filter chain is continued.
  uint64_t data_size_before_continue_{};
  Network::ReadFilterCallbacks* read_callbacks_{};
};

class PauseFilterFactory : public Extensions::NetworkFilters::Common::FactoryBase<
                               test::integration::tcp_proxy::PauseFilterConfig> {
public:
  PauseFilterFactory() : FactoryBase("test.pause_iteration") {}

private:
  Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const test::integration::tcp_proxy::PauseFilterConfig& cfg,
                                    Server::Configuration::FactoryContext&) override {
    int data_size_before_continue = cfg.data_size_before_continue();
    return [data_size_before_continue = std::move(data_size_before_continue)](
               Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<PauseFilter>(data_size_before_continue));
    };
  }
};

class TcpProxyReceiveBeforeConnectIntegrationTest : public TcpProxyIntegrationTest {
public:
  void addPauseFilter(uint32_t data_size_before_continue) {
    config_helper_.addNetworkFilter(fmt::format(R"EOF(
      name: test.pause_iteration
      typed_config:
        "@type": type.googleapis.com/test.integration.tcp_proxy.PauseFilterConfig
        data_size_before_continue: {}
)EOF",
                                                data_size_before_continue));
  }

  PauseFilterFactory factory_;
  Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory> register_factory_{
      factory_};
};

INSTANTIATE_TEST_SUITE_P(TcpProxyIntegrationTestParams, TcpProxyReceiveBeforeConnectIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(TcpProxyReceiveBeforeConnectIntegrationTest, ReceiveBeforeConnectEarlyData) {
  uint32_t data_size_before_continue = 6;
  addPauseFilter(data_size_before_continue);

  initialize();
  FakeRawConnectionPtr fake_upstream_connection;
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Until total data size > 6 is received, the PauseFilter stops the iteration. Downstream counter
  // is incremented, but no connection attempt to upstream is made.
  ASSERT_TRUE(tcp_client->write("hello"));
  test_server_->waitForCounterEq("tcp.tcpproxy_stats.downstream_cx_total", 1);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());

  ASSERT_TRUE(tcp_client->write("world"));
  test_server_->waitForCounterEq("tcp.tcpproxy_stats.early_data_received_count_total", 1);
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());

  // Once the connection is established, the early data will be flushed to the upstream.
  ASSERT_TRUE(fake_upstream_connection->waitForData(FakeRawConnection::waitForMatch("helloworld")));
  ASSERT_TRUE(fake_upstream_connection->write("response"));
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(TcpProxyReceiveBeforeConnectIntegrationTest, UpstreamBufferHighWatermark) {
  /* This test validates that no inconsistent state happens when the downstream is read disabled
   * twice, once when the early data is received and next when the upstream buffer hits high
   * watermark.
   */
  const uint32_t data_size_before_continue = 512;
  addPauseFilter(data_size_before_continue);

  const uint32_t upstream_buffer_limit = 512;
  const uint32_t downstream_buffer_limit = 1024;
  const uint32_t data_size = 16 * downstream_buffer_limit;

  config_helper_.setBufferLimits(upstream_buffer_limit, downstream_buffer_limit);
  std::string data;
  for (uint32_t i = 0; i < data_size / 4; i++)
    data += "abcd";

  initialize();
  FakeRawConnectionPtr fake_upstream_connection;
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // PauseFilter stops the iteration until sufficient data is received.
  ASSERT_TRUE(tcp_client->write(data.substr(0, upstream_buffer_limit - 1)));
  test_server_->waitForCounterEq("tcp.tcpproxy_stats.downstream_cx_total", 1);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());

  // Downstream sends more data. PauseFilter allows the iteration to continue, upstream connection
  // is established. The buffered early data is sent to the upstream.
  ASSERT_TRUE(tcp_client->write(data.substr(upstream_buffer_limit - 1)));
  test_server_->waitForCounterEq("tcp.tcpproxy_stats.early_data_received_count_total", 1);
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());

  // At this point, the downstream is already read disabled, waiting for early data flush. Another
  // downstream read disable is triggered as the early data is pushed to the upstream buffer and the
  // buffer hits the high watermark. Downstream read disable counter will be 2. After the early data
  // is pushed, downstream is read enabled once. Downstream read disable counter will be 1. After
  // the upstream buffer is flushed to the upstream, it comes below the watermark and downstream is
  // read enabled.
  ASSERT_TRUE(fake_upstream_connection->waitForData(FakeRawConnection::waitForMatch(data.c_str())));
  ASSERT_TRUE(fake_upstream_connection->waitForData(data_size));
  ASSERT_TRUE(fake_upstream_connection->write("response"));
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForHalfClose();
  tcp_client->close();

  uint32_t upstream_pauses =
      test_server_->counter("cluster.cluster_0.upstream_flow_control_paused_reading_total")
          ->value();
  uint32_t upstream_resumes =
      test_server_->counter("cluster.cluster_0.upstream_flow_control_resumed_reading_total")
          ->value();

  uint32_t downstream_pauses =
      test_server_->counter("tcp.tcpproxy_stats.downstream_flow_control_paused_reading_total")
          ->value();
  uint32_t downstream_resumes =
      test_server_->counter("tcp.tcpproxy_stats.downstream_flow_control_resumed_reading_total")
          ->value();

  EXPECT_EQ(upstream_pauses, upstream_resumes);
  EXPECT_EQ(upstream_resumes, 0);

  // Since we are receiving early data, downstream connection will already be read
  // disabled so downstream pause metric is not emitted when upstream buffer hits high
  // watermark. When the upstream buffer watermark goes down, downstream will be read
  // enabled and downstream resume metric will be emitted.
  EXPECT_EQ(downstream_pauses, 0);
  EXPECT_EQ(downstream_resumes, 1);
}

// Test ON_DOWNSTREAM_DATA mode delays connection until data is received.
TEST_P(TcpProxyIntegrationTest, UpstreamConnectModeOnDownstreamData) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(8192);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // No upstream connection should be established yet.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_FALSE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection,
                                                        std::chrono::milliseconds(500)));

  // Send data - this should trigger upstream connection.
  ASSERT_TRUE(tcp_client->write("trigger", false));

  // Now upstream connection should be established.
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // The buffered data should be forwarded.
  ASSERT_TRUE(fake_upstream_connection->waitForData(7));

  // Send more data both ways.
  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(12));
  ASSERT_TRUE(fake_upstream_connection->write("world", true));
  tcp_client->waitForData("world");
  tcp_client->waitForHalfClose();

  tcp_client->close();
}

// Test early data buffering with half-close.
TEST_P(TcpProxyIntegrationTest, UpstreamConnectModeEarlyDataWithHalfClose) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(8192);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Send data with half-close.
  ASSERT_TRUE(tcp_client->write("final_data", true)); // end_stream = true

  // Upstream connection should be established.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Data should be forwarded with half-close.
  std::string received_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(10, &received_data));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  EXPECT_EQ("final_data", received_data);

  // Upstream can still send data back.
  ASSERT_TRUE(fake_upstream_connection->write("response", true));
  tcp_client->waitForData("response");
  tcp_client->waitForHalfClose();

  tcp_client->close();
}

// Test multiple concurrent connections with ON_DOWNSTREAM_DATA mode.
TEST_P(TcpProxyIntegrationTest, UpstreamConnectModeMultipleConcurrent) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(8192);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  // First connection.
  IntegrationTcpClientPtr tcp_client1 = makeTcpConnection(lookupPort("tcp_proxy"));

  // Second connection.
  IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("tcp_proxy"));

  // No upstream connections yet.
  FakeRawConnectionPtr fake_upstream_connection1;
  FakeRawConnectionPtr fake_upstream_connection2;

  // First client sends data.
  ASSERT_TRUE(tcp_client1->write("client1", false));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection1));
  ASSERT_TRUE(fake_upstream_connection1->waitForData(7));

  // Second client sends data.
  ASSERT_TRUE(tcp_client2->write("client2", false));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection2));
  ASSERT_TRUE(fake_upstream_connection2->waitForData(7));

  // Verify data isolation.
  std::string client1_data;
  std::string client2_data;
  ASSERT_TRUE(fake_upstream_connection1->waitForData(7, &client1_data));
  ASSERT_TRUE(fake_upstream_connection2->waitForData(7, &client2_data));
  EXPECT_EQ("client1", client1_data);
  EXPECT_EQ("client2", client2_data);

  ASSERT_TRUE(fake_upstream_connection1->close());
  ASSERT_TRUE(fake_upstream_connection1->waitForDisconnect());
  tcp_client1->waitForHalfClose();

  ASSERT_TRUE(fake_upstream_connection2->close());
  ASSERT_TRUE(fake_upstream_connection2->waitForDisconnect());
  tcp_client2->waitForHalfClose();

  tcp_client1->close();
  tcp_client2->close();
}

// Test ON_DOWNSTREAM_DATA mode with non-TLS connection.
TEST_P(TcpProxyIntegrationTest, UpstreamConnectModeOnDownstreamDataNonTls) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(8192);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // For non-TLS connections, this mode behaves as ON_DOWNSTREAM_DATA.
  // Send data - should trigger connection immediately (no TLS to wait for).
  ASSERT_TRUE(tcp_client->write("data", false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Buffered data should be forwarded after connection.
  ASSERT_TRUE(fake_upstream_connection->waitForData(4));

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

// Test downstream close before upstream establishment.
TEST_P(TcpProxyIntegrationTest, UpstreamConnectModeDownstreamCloseBeforeUpstream) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(8192);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Close connection before sending data.
  tcp_client->close();

  // No upstream connection should be established.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_FALSE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection,
                                                        std::chrono::milliseconds(500)));
}

// Test ON_DOWNSTREAM_TLS_HANDSHAKE mode with non-TLS connection.
TEST_P(TcpProxyIntegrationTest, UpstreamConnectModeTlsHandshakeNonTls) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_TLS_HANDSHAKE);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(0);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // For non-TLS connection, should establish upstream immediately despite TLS mode.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send data both ways to verify connection works.
  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world", true));
  tcp_client->waitForData("world");
  tcp_client->waitForHalfClose();

  tcp_client->close();
}

// Test ON_DOWNSTREAM_TLS_HANDSHAKE mode with upstream TLS (simpler test).
// This tests the mode without downstream TLS which should behave as IMMEDIATE.
// Full TLS downstream testing would require more complex test infrastructure.
TEST_P(TcpProxyIntegrationTest, UpstreamConnectModeTlsHandshakeWithUpstreamTls) {
  upstream_tls_ = true;
  setUpstreamProtocol(Http::CodecType::HTTP1);
  config_helper_.configureUpstreamTls();

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_TLS_HANDSHAKE);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(0);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  // Create non-TLS client connection (should connect immediately).
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // The upstream connection should be established immediately (no downstream TLS).
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send data both ways to verify connection works.
  ASSERT_TRUE(tcp_client->write("hello_tls", false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(9));
  ASSERT_TRUE(fake_upstream_connection->write("world_tls", true));
  tcp_client->waitForData("world_tls");
  tcp_client->waitForHalfClose();

  tcp_client->close();
}

// Test ON_DOWNSTREAM_TLS_HANDSHAKE mode with max_early_data_bytes set to zero.
TEST_P(TcpProxySslIntegrationTest, OnDownstreamTlsHandshakeMode) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>());
    auto tcp_proxy_config =
        MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
            *config_blob);

    tcp_proxy_config.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_TLS_HANDSHAKE);
    tcp_proxy_config.mutable_max_early_data_bytes()->set_value(0);

    config_blob->PackFrom(tcp_proxy_config);
  });

  setupConnections();
  sendAndReceiveTlsData("hello", "world");
}

// Test ON_DOWNSTREAM_TLS_HANDSHAKE mode with early data buffering.
TEST_P(TcpProxySslIntegrationTest, OnDownstreamTlsHandshakeModeWithEarlyData) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>());
    auto tcp_proxy_config =
        MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
            *config_blob);
    // Set ON_DOWNSTREAM_TLS_HANDSHAKE mode with max_early_data_bytes.
    tcp_proxy_config.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_TLS_HANDSHAKE);
    tcp_proxy_config.mutable_max_early_data_bytes()->set_value(16384);

    config_blob->PackFrom(tcp_proxy_config);
  });

  initialize();

  MockWatermarkBuffer* client_write_buffer = nullptr;
  ConnectionStatusCallbacks connect_callbacks;
  std::shared_ptr<WaitForPayloadReader> payload_reader =
      std::make_shared<WaitForPayloadReader>(*dispatcher_);
  Network::ClientConnectionPtr ssl_client;
  FakeRawConnectionPtr fake_upstream_connection;

  // Set up the mock buffer factory so the newly created SSL client will have a mock write
  // buffer. This allows us to track the bytes actually written to the socket.
  EXPECT_CALL(*mock_buffer_factory_, createBuffer_(_, _, _))
      .Times(AtLeast(1))
      .WillOnce(Invoke([&](std::function<void()> below_low, std::function<void()> above_high,
                           std::function<void()> above_overflow) -> Buffer::Instance* {
        client_write_buffer =
            new NiceMock<MockWatermarkBuffer>(below_low, above_high, above_overflow);
        ON_CALL(*client_write_buffer, move(_))
            .WillByDefault(Invoke(client_write_buffer, &MockWatermarkBuffer::baseMove));
        ON_CALL(*client_write_buffer, drain(_))
            .WillByDefault(Invoke(client_write_buffer, &MockWatermarkBuffer::trackDrains));
        return client_write_buffer;
      }));

  // Set up the SSL client.
  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("tcp_proxy"));
  ssl_client = dispatcher_->createClientConnection(
      address, Network::Address::InstanceConstSharedPtr(),
      context_->createTransportSocket(nullptr, nullptr), nullptr, nullptr);

  // Perform the SSL handshake. Loopback is allowlisted in tcp_proxy.json for the ssl_auth
  // filter so there will be no pause waiting on auth data.
  ssl_client->addConnectionCallbacks(connect_callbacks);
  ssl_client->enableHalfClose(true);
  ssl_client->addReadFilter(payload_reader);
  ssl_client->connect();

  // No upstream connection should be established yet (before TLS handshake completes).
  ASSERT_FALSE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection,
                                                        std::chrono::milliseconds(500)));

  // Wait for TLS handshake to complete. The Connected event fires after TLS handshake.
  while (!connect_callbacks.connected()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Now upstream connection should be established after TLS handshake completes.
  AssertionResult result = fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection);
  RELEASE_ASSERT(result, result.message());

  // Send data after TLS handshake - this should be buffered if upstream isn't ready yet.
  Buffer::OwnedImpl buffer("hello");
  ssl_client->write(buffer, false);
  while (client_write_buffer->bytesDrained() != 5) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Verify data is forwarded (buffered and then sent once upstream is ready).
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // Send response back.
  ASSERT_TRUE(fake_upstream_connection->write("world", true));
  payload_reader->setDataToWaitFor("world");
  ssl_client->dispatcher().run(Event::Dispatcher::RunType::Block);

  // Clean up.
  Buffer::OwnedImpl empty_buffer;
  ssl_client->write(empty_buffer, true);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  ssl_client->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(payload_reader->readLastByte());
  EXPECT_TRUE(connect_callbacks.closed());
}

// Test connection close during wait for data trigger.
TEST_P(TcpProxyIntegrationTest, UpstreamConnectModeConnectionCloseDuringWait) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(65536);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  // Create non-TLS client connection.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // The upstream connection should NOT be established yet (waiting for data).
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_FALSE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection,
                                                        std::chrono::milliseconds(500)));

  // Close connection before sending data.
  tcp_client->close();

  // Verify no upstream connection was made.
  ASSERT_FALSE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection,
                                                        std::chrono::milliseconds(500)));
}

// Test IMMEDIATE mode.
TEST_P(TcpProxyIntegrationTest, UpstreamConnectModeImmediate) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::IMMEDIATE);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Upstream connection should be established immediately.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send data both ways.
  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world", true));
  tcp_client->waitForData("world");
  tcp_client->waitForHalfClose();

  tcp_client->close();
}

// Test orthogonality: TLS_HANDSHAKE mode with max_early_data_bytes enabled.
TEST_P(TcpProxyIntegrationTest, TlsHandshakeModeWithEarlyDataOrthogonality) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    // TLS_HANDSHAKE mode with early data buffering (orthogonal features).
    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_TLS_HANDSHAKE);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(4096);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  // Non-TLS connection - should fall back to IMMEDIATE mode.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Upstream connection should be established immediately (fallback behavior).
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("test"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(4));

  tcp_client->close();
}

// Test single connection trigger guarantee with rapid data chunks.
TEST_P(TcpProxyIntegrationTest, SingleConnectionTriggerWithRapidDataChunks) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(8192);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // No upstream connection should exist yet.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_FALSE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection,
                                                        std::chrono::milliseconds(500)));

  // Send multiple rapid data chunks.
  ASSERT_TRUE(tcp_client->write("chunk1"));
  ASSERT_TRUE(tcp_client->write("chunk2"));
  ASSERT_TRUE(tcp_client->write("chunk3"));

  // Wait for upstream connection (should be triggered by first chunk).
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // All data should be received.
  ASSERT_TRUE(fake_upstream_connection->waitForData(18));

  tcp_client->close();
}

// Test buffer overflow scenario. The connection should remain stable and not
// trigger a new connection.
TEST_P(TcpProxyIntegrationTest, BufferOverflowStability) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    // Small buffer to trigger overflow.
    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(100);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Send data that exceeds buffer limit.
  std::string large_data(200, 'X');
  ASSERT_TRUE(tcp_client->write(large_data));

  // Upstream connection should still be established.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // All data should eventually be received (may be in chunks).
  ASSERT_TRUE(fake_upstream_connection->waitForData(200));

  tcp_client->close();
}

// Test bidirectional data flow after delayed connection establishment.
TEST_P(TcpProxyIntegrationTest, BidirectionalFlowAfterDelayedConnection) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(8192);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Send initial data to trigger connection.
  ASSERT_TRUE(tcp_client->write("hello"));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // Test bidirectional flow.
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");

  ASSERT_TRUE(tcp_client->write("again"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(10));

  tcp_client->close();
}

// Test that connection without any data does not trigger upstream in ON_DOWNSTREAM_DATA mode.
TEST_P(TcpProxyIntegrationTest, NoDataDoesNotTriggerConnection) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(8192);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Wait to ensure no connection is established without data.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_FALSE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection,
                                                        std::chrono::milliseconds(1000)));

  // Now send actual data.
  ASSERT_TRUE(tcp_client->write("data"));

  // Connection should now be established.
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(4));

  tcp_client->close();
}

// Test edge case where max_buffered_bytes=0 with ON_DOWNSTREAM_DATA mode.
TEST_P(TcpProxyIntegrationTest, ZeroBufferWithOnDownstreamData) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(0);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Send minimal data which should trigger connection and be buffered/sent.
  // With max_buffered_bytes=0, readDisable will be called immediately,
  // but the connection should still be established.
  ASSERT_TRUE(tcp_client->write("a"));

  // Connection should be established.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(1));

  // Send more data.
  ASSERT_TRUE(tcp_client->write("bcd"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(4));

  tcp_client->close();
}

// Test backward compatibility. By default the configuration works as before.
TEST_P(TcpProxyIntegrationTest, BackwardCompatibilityDefaultConfig) {
  // Use default configuration (no upstream_connect_mode or max_early_data_bytes).
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Should behave like traditional TCP proxy - immediate connection.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("test"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(4));

  tcp_client->close();
}

// Test large payload with ON_DOWNSTREAM_DATA mode.
TEST_P(TcpProxyIntegrationTest, LargePayloadWithOnDownstreamData) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(65536);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Send 64KB of data.
  std::string large_payload(65536, 'L');
  ASSERT_TRUE(tcp_client->write(large_payload));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // All data should be received.
  ASSERT_TRUE(fake_upstream_connection->waitForData(65536));

  tcp_client->close();
}

// Test connection close before data arrives in ON_DOWNSTREAM_DATA mode.
TEST_P(TcpProxyIntegrationTest, ConnectionCloseBeforeDataInOnDownstreamData) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(8192);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Close connection without sending data.
  tcp_client->close();

  // No upstream connection should have been established.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_FALSE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection,
                                                        std::chrono::milliseconds(1000)));
}

// Test multiple concurrent connections with ON_DOWNSTREAM_DATA mode.
TEST_P(TcpProxyIntegrationTest, MultipleConcurrentConnectionsWithOnDownstreamData) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(8192);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  // Create 3 concurrent connections.
  IntegrationTcpClientPtr tcp_client1 = makeTcpConnection(lookupPort("tcp_proxy"));
  IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("tcp_proxy"));
  IntegrationTcpClientPtr tcp_client3 = makeTcpConnection(lookupPort("tcp_proxy"));

  // Send data on all connections.
  ASSERT_TRUE(tcp_client1->write("client1"));
  ASSERT_TRUE(tcp_client2->write("client2"));
  ASSERT_TRUE(tcp_client3->write("client3"));

  // All upstream connections should be established.
  FakeRawConnectionPtr fake_upstream_connection1;
  FakeRawConnectionPtr fake_upstream_connection2;
  FakeRawConnectionPtr fake_upstream_connection3;

  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection1));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection2));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection3));

  // Verify data received on each connection.
  ASSERT_TRUE(fake_upstream_connection1->waitForData(7));
  ASSERT_TRUE(fake_upstream_connection2->waitForData(7));
  ASSERT_TRUE(fake_upstream_connection3->waitForData(7));

  // Clean up.
  tcp_client1->close();
  tcp_client2->close();
  tcp_client3->close();
}

// Test downstream closes immediately in IMMEDIATE mode (race condition coverage).
// The upstream connection may or may not be established depending on timing.
TEST_P(TcpProxyIntegrationTest, DownstreamClosedImmediatelyInImmediateMode) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::IMMEDIATE);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  // Create connection and close immediately to trigger race condition.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  tcp_client->close();
}

// Test downstream closes with data after triggering connection in ON_DOWNSTREAM_DATA mode.
TEST_P(TcpProxyIntegrationTest, DownstreamClosedAfterDataInOnDownstreamDataMode) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(1024);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  // Create connection, send data to trigger upstream connection, then close immediately.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("trigger", false, false));

  // Close immediately after writing, creating race with upstream connection establishment.
  tcp_client->close();
}

// Test downstream closes with buffered data before upstream is ready.
TEST_P(TcpProxyIntegrationTest, DownstreamClosedWithBufferedDataBeforeUpstreamReady) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    tcp_proxy.set_upstream_connect_mode(
        envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
    tcp_proxy.mutable_max_early_data_bytes()->set_value(8192);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  // Create connection, send substantial data, then close immediately.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  std::string data(4096, 'x');
  ASSERT_TRUE(tcp_client->write(data, false, false));

  // Close with buffered data before upstream connection completes.
  tcp_client->close();
}

// Test that validates that upstream connection don't leak when downstream closes with
// end_stream but no data.
TEST_P(TcpProxyIntegrationTest, DownstreamClosedWithEndStreamNoData) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    filter->typed_config().UnpackTo(&tcp_proxy);

    // Set max_early_data_bytes to enable receive_before_connect behavior.
    tcp_proxy.mutable_max_early_data_bytes()->set_value(1024);

    filter->mutable_typed_config()->PackFrom(tcp_proxy);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Close without sending data but with end_stream=true (FIN).
  tcp_client->close();
}

} // namespace Envoy
