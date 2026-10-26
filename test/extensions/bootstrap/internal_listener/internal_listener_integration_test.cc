#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/bootstrap/internal_listener/v3/internal_listener.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/network/connection.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/config/api_version.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/base_integration_test.h"
#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;
namespace Envoy {
namespace {

class InternalListenerIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                        public BaseIntegrationTest {
public:
  InternalListenerIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}

  void initialize() override {
    config_helper_.renameListener("tcp");
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto& listener = *bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener.mutable_internal_listener();
      listener.clear_address();
    });
    config_helper_.addBootstrapExtension(R"EOF(
name: envoy.bootstrap.internal_listener
typed_config:
  "@type": "type.googleapis.com/envoy.extensions.bootstrap.internal_listener.v3.InternalListener"
)EOF");
    BaseIntegrationTest::initialize();
  }
};

TEST_P(InternalListenerIntegrationTest, BasicConfigUpdate) {
  initialize();
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        (*(*listener->mutable_metadata()->mutable_filter_metadata())["random_filter_name"]
              .mutable_fields())["random_key"]
            .set_number_value(1);
        listener->clear_address();
      });

  new_config_helper.setLds("1");

  test_server_->waitForCounter("listener_manager.listener_modified", Eq(1));
  test_server_->waitForGauge("listener_manager.total_listeners_draining", Eq(0));
}

TEST_P(InternalListenerIntegrationTest, InplaceUpdate) {
  initialize();
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        auto new_filter_chain = *listener->mutable_filter_chains(0);
        listener->mutable_filter_chains()->Add()->MergeFrom(new_filter_chain);
        *(listener->mutable_filter_chains(1)
              ->mutable_filter_chain_match()
              ->mutable_application_protocols()
              ->Add()) = "alpn";
        listener->clear_address();
      });

  new_config_helper.setLds("1");

  test_server_->waitForCounter("listener_manager.listener_modified", Eq(1));
  test_server_->waitForGauge("listener_manager.total_listeners_draining", Eq(0));
}

TEST_P(InternalListenerIntegrationTest, DeleteListener) {
  initialize();
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        bootstrap.mutable_static_resources()->mutable_listeners()->RemoveLast();
      });

  new_config_helper.setLds("1");

  test_server_->waitForCounter("listener_manager.listener_removed", Eq(1));
  test_server_->waitForGauge("listener_manager.total_listeners_draining", Eq(0));
}

INSTANTIATE_TEST_SUITE_P(IpVersions, InternalListenerIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

class InternalListenerResetIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  InternalListenerResetIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {
    enableHalfClose(false);
  }

  void setupAccessLog() {
    real_access_log_path_ = TestEnvironment::temporaryPath(fmt::format(
        "real_access_log{}{}.txt", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6",
        TestUtility::uniqueFilename()));
    internal_access_log_path_ = TestEnvironment::temporaryPath(fmt::format(
        "internal_access_log{}{}.txt", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6",
        TestUtility::uniqueFilename()));
  }

  void initialize() override {
    config_helper_.renameListener("real_listener");
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add internal listener bootstrap extension.
      auto* bootstrap_extension = bootstrap.add_bootstrap_extensions();
      bootstrap_extension->set_name("envoy.bootstrap.internal_listener");
      envoy::extensions::bootstrap::internal_listener::v3::InternalListener
          internal_listener_config;
      std::ignore = bootstrap_extension->mutable_typed_config()->PackFrom(internal_listener_config);

      // Add internal cluster pointing to the internal listener.
      auto* internal_cluster = bootstrap.mutable_static_resources()->add_clusters();
      internal_cluster->set_name("cluster_internal");
      auto* load_assignment = internal_cluster->mutable_load_assignment();
      load_assignment->set_cluster_name("cluster_internal");
      load_assignment->add_endpoints()
          ->add_lb_endpoints()
          ->mutable_endpoint()
          ->mutable_address()
          ->mutable_envoy_internal_address()
          ->set_server_listener_name("internal_listener");

      // Configure real listener (listener 0) to route to "cluster_internal".
      auto* config_blob = bootstrap.mutable_static_resources()
                              ->mutable_listeners(0)
                              ->mutable_filter_chains(0)
                              ->mutable_filters(0)
                              ->mutable_typed_config();
      auto tcp_proxy_config =
          MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
              *config_blob);
      tcp_proxy_config.set_cluster("cluster_internal");
      tcp_proxy_config.set_stat_prefix("tcp_proxy_real");
      if (!real_access_log_path_.empty()) {
        auto* access_log = tcp_proxy_config.add_access_log();
        access_log->set_name("accesslog_real");
        envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
        access_log_config.set_path(real_access_log_path_);
        access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
            "%UPSTREAM_DETECTED_CLOSE_TYPE%");
        std::ignore = access_log->mutable_typed_config()->PackFrom(access_log_config);
      }
      std::ignore = config_blob->PackFrom(tcp_proxy_config);

      // Add the internal listener that routes to "cluster_0" (fake upstream).
      auto* internal_listener = bootstrap.mutable_static_resources()->add_listeners();
      internal_listener->set_name("internal_listener");
      internal_listener->mutable_internal_listener();
      auto* internal_filter = internal_listener->add_filter_chains()->add_filters();
      internal_filter->set_name("tcp_proxy");
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy internal_tcp_proxy;
      internal_tcp_proxy.set_cluster("cluster_0");
      internal_tcp_proxy.set_stat_prefix("tcp_proxy_internal");
      if (!internal_access_log_path_.empty()) {
        auto* access_log = internal_tcp_proxy.add_access_log();
        access_log->set_name("accesslog_internal");
        envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
        access_log_config.set_path(internal_access_log_path_);
        access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
            "%DOWNSTREAM_DETECTED_CLOSE_TYPE%");
        std::ignore = access_log->mutable_typed_config()->PackFrom(access_log_config);
      }
      std::ignore = internal_filter->mutable_typed_config()->PackFrom(internal_tcp_proxy);
    });

    BaseIntegrationTest::initialize();
  }

  std::string real_access_log_path_;
  std::string internal_access_log_path_;
};

#if ENVOY_PLATFORM_ENABLE_SEND_RST
// Verifies that downstream RST sent to the real listener propagates across the internal listener
// to the upstream connection as an RST.
TEST_P(InternalListenerResetIntegrationTest, DownstreamRstPropagation) {
  setupAccessLog();
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("real_listener"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // Downstream sends RST.
  tcp_client->close(Network::ConnectionCloseType::AbortReset);

  // Upstream should receive RST through the internal listener.
  ASSERT_TRUE(fake_upstream_connection->waitForRstDisconnect());

  // Internal listener should detect the downstream reset from the user-space socket.
  auto log_result = waitForAccessLog(internal_access_log_path_);
  EXPECT_THAT(log_result, testing::Eq("RemoteReset"));
}

// Verifies that when the runtime feature is disabled, downstream RST is NOT propagated as RST
// (it closes gracefully with a FIN instead).
TEST_P(InternalListenerResetIntegrationTest, DownstreamRstPropagationDisabled) {
  setupAccessLog();
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.enable_send_rst_on_user_space_socket", "false");
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("real_listener"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // Downstream sends RST.
  tcp_client->close(Network::ConnectionCloseType::AbortReset);

  // Upstream should disconnect normally (FIN), NOT an RST disconnect.
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  // Internal listener should detect a normal downstream close (not RemoteReset).
  auto log_result = waitForAccessLog(internal_access_log_path_);
  EXPECT_THAT(log_result, testing::Eq("Normal"));
}

// Verifies that upstream RST propagates back across the internal listener to the real listener
// downstream client.
TEST_P(InternalListenerResetIntegrationTest, UpstreamRstPropagation) {
  setupAccessLog();
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("real_listener"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // Upstream sends RST.
  ASSERT_TRUE(fake_upstream_connection->close(Network::ConnectionCloseType::AbortReset));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  // Downstream should receive disconnect.
  tcp_client->waitForDisconnect();

  // Real listener should detect the upstream reset propagated across the internal listener.
  auto log_result = waitForAccessLog(real_access_log_path_);
  EXPECT_THAT(log_result, testing::Eq("RemoteReset"));
}

// Verifies that when downstream sends half-close (FIN) across the internal listener, followed by
// a full close from the upstream, both listeners detect a Normal close rather than RemoteReset.
TEST_P(InternalListenerResetIntegrationTest, DownstreamHalfClosePreservesNormalClose) {
  setupAccessLog();
  enableHalfClose(true);
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("real_listener"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Downstream sends data with end_stream=true (half-close).
  ASSERT_TRUE(tcp_client->write("hello", /*end_stream=*/true));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  // Upstream sends response data and closes gracefully.
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  ASSERT_TRUE(fake_upstream_connection->close(Network::ConnectionCloseType::FlushWrite));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  // Downstream should receive response data and disconnect.
  tcp_client->waitForData("world");
  tcp_client->waitForDisconnect();

  // Internal listener should detect a normal downstream close, NOT RemoteReset.
  auto internal_log = waitForAccessLog(internal_access_log_path_);
  EXPECT_THAT(internal_log, testing::Eq("Normal"));
  // Real listener should detect a normal upstream close, NOT RemoteReset.
  auto real_log = waitForAccessLog(real_access_log_path_);
  EXPECT_THAT(real_log, testing::Eq("Normal"));
}
#endif

INSTANTIATE_TEST_SUITE_P(IpVersions, InternalListenerResetIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// An HTTP listener routes to an internal listener whose tcp_proxy tunnels the bytes to the fake
// upstream over HTTP CONNECT. Verifies that the reason tcp_proxy fails to establish the tunnel is
// reported to the HTTP router instead of a bare connection termination.
class InternalListenerTunnelFailureIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  InternalListenerTunnelFailureIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }

  void initialize() override {
    useAccessLog("%RESPONSE_CODE% %RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS% "
                 "%UPSTREAM_TRANSPORT_FAILURE_REASON%");
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* bootstrap_extension = bootstrap.add_bootstrap_extensions();
      bootstrap_extension->set_name("envoy.bootstrap.internal_listener");
      envoy::extensions::bootstrap::internal_listener::v3::InternalListener
          internal_listener_config;
      std::ignore = bootstrap_extension->mutable_typed_config()->PackFrom(internal_listener_config);

      // The cluster of the HTTP route, which points to the internal listener.
      auto* internal_cluster = bootstrap.mutable_static_resources()->add_clusters();
      internal_cluster->set_name("cluster_internal");
      auto* load_assignment = internal_cluster->mutable_load_assignment();
      load_assignment->set_cluster_name("cluster_internal");
      load_assignment->add_endpoints()
          ->add_lb_endpoints()
          ->mutable_endpoint()
          ->mutable_address()
          ->mutable_envoy_internal_address()
          ->set_server_listener_name("internal_listener");

      // The internal listener tunnels the bytes to cluster_0 (the fake upstream) over CONNECT.
      auto* internal_listener = bootstrap.mutable_static_resources()->add_listeners();
      internal_listener->set_name("internal_listener");
      internal_listener->mutable_internal_listener();
      auto* internal_filter = internal_listener->add_filter_chains()->add_filters();
      internal_filter->set_name("tcp_proxy");
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
      tcp_proxy.set_cluster("cluster_0");
      tcp_proxy.set_stat_prefix("tcp_proxy_internal");
      tcp_proxy.mutable_tunneling_config()->set_hostname("actor.example.com:443");
      std::ignore = internal_filter->mutable_typed_config()->PackFrom(tcp_proxy);
    });
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_route()
              ->set_cluster("cluster_internal");
        });
    HttpIntegrationTest::initialize();
  }

  // Sends a request, and has the fake upstream reject the CONNECT request of the internal
  // listener's tcp_proxy with the given status. Returns the access log entry of the request.
  std::string rejectTunnel(Http::Code status) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

    EXPECT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    EXPECT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    EXPECT_TRUE(upstream_request_->waitForHeadersComplete());
    EXPECT_EQ("CONNECT", upstream_request_->headers().getMethodValue());
    upstream_request_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", std::to_string(enumToInt(status))}}, false);
    // tcp_proxy resets the rejected CONNECT stream. Wait for the fake upstream to process the reset
    // before the stream can be destroyed, as the fake upstream runs on its own thread.
    EXPECT_TRUE(upstream_request_->waitForReset());

    EXPECT_TRUE(response->waitForEndStream());
    EXPECT_EQ("503", response->headers().getStatusValue());
    codec_client_->close();
    return waitForAccessLog(access_log_name_);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, InternalListenerTunnelFailureIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(InternalListenerTunnelFailureIntegrationTest, TunnelFailureReasonReachesRouter) {
  initialize();
  const std::string log = rejectTunnel(Http::Code::MisdirectedRequest);
  // tcp_proxy closes the internal connection with an initialization failure reason, here
  // UpstreamFailureReason::ConnectFailed (0), after the upstream rejected its CONNECT request.
  const std::string close_details =
      absl::StrCat("internal_listener_close:",
                   StreamInfo::LocalCloseReasons::get().TcpProxyInitializationFailure,
                   "0|internal_listener_upstream_failure:tunnel_response:421");
  EXPECT_THAT(log, testing::HasSubstr(absl::StrCat(
                       "503 UC upstream_reset_before_response_started{connection_termination|",
                       close_details, "} ", close_details)));
}

TEST_P(InternalListenerTunnelFailureIntegrationTest, PropagationDisabledOnListenerSide) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.internal_listener_propagate_close_details", "false");
  initialize();
  EXPECT_THAT(rejectTunnel(Http::Code::MisdirectedRequest),
              testing::HasSubstr("503 UC upstream_reset_before_response_started{connection_"
                                 "termination} -"));
}

TEST_P(InternalListenerTunnelFailureIntegrationTest, PropagationDisabledOnRouterSide) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.router_reset_with_connection_failure_reason", "false");
  initialize();
  EXPECT_THAT(rejectTunnel(Http::Code::MisdirectedRequest),
              testing::HasSubstr("503 UC upstream_reset_before_response_started{connection_"
                                 "termination} -"));
}

} // namespace
} // namespace Envoy
