#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/bootstrap/internal_listener/v3/internal_listener.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/network/connection.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/api_version.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/base_integration_test.h"
#include "test/integration/http_integration.h"
#include "test/integration/ssl_utility.h"
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

void configureInternalTlsPair(envoy::config::cluster::v3::Cluster& internal_cluster,
                              envoy::config::listener::v3::FilterChain& internal_chain) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext upstream_tls;
  Ssl::initializeUpstreamTlsContextConfig(Ssl::ClientSslTransportOptions(), upstream_tls,
                                          /*connect_to_upstream=*/true);
  internal_cluster.mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
  std::ignore =
      internal_cluster.mutable_transport_socket()->mutable_typed_config()->PackFrom(upstream_tls);

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext downstream_tls;
  auto* tls_cert = downstream_tls.mutable_common_tls_context()->add_tls_certificates();
  tls_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
  tls_cert->mutable_private_key()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));
  internal_chain.mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
  std::ignore =
      internal_chain.mutable_transport_socket()->mutable_typed_config()->PackFrom(downstream_tls);
}

// Real listener tcp_proxy -> internal listener tcp_proxy tunneling over HTTP/2 CONNECT.
class InternalListenerTunnelingCloseIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  InternalListenerTunnelingCloseIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam(), ConfigHelper::tcpProxyConfig()) {
    enableHalfClose(true);
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }

  void initialize() override {
    real_access_log_path_ = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
    config_helper_.renameListener("real_listener");
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* bootstrap_extension = bootstrap.add_bootstrap_extensions();
      bootstrap_extension->set_name("envoy.bootstrap.internal_listener");
      envoy::extensions::bootstrap::internal_listener::v3::InternalListener
          internal_listener_config;
      std::ignore = bootstrap_extension->mutable_typed_config()->PackFrom(internal_listener_config);

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
      auto* access_log = tcp_proxy_config.add_access_log();
      access_log->set_name("accesslog_real");
      envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
      access_log_config.set_path(real_access_log_path_);
      access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
          "%UPSTREAM_DETECTED_CLOSE_TYPE% %RESPONSE_FLAGS%");
      std::ignore = access_log->mutable_typed_config()->PackFrom(access_log_config);
      std::ignore = config_blob->PackFrom(tcp_proxy_config);

      auto* internal_listener = bootstrap.mutable_static_resources()->add_listeners();
      internal_listener->set_name("internal_listener");
      internal_listener->mutable_internal_listener();
      auto* internal_chain = internal_listener->add_filter_chains();
      auto* internal_filter = internal_chain->add_filters();
      internal_filter->set_name("tcp_proxy");
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy internal_tcp_proxy;
      internal_tcp_proxy.set_cluster("cluster_0");
      internal_tcp_proxy.set_stat_prefix("tcp_proxy_internal");
      internal_tcp_proxy.mutable_tunneling_config()->set_hostname("foo.lyft.com:80");
      std::ignore = internal_filter->mutable_typed_config()->PackFrom(internal_tcp_proxy);

      if (enable_internal_tls_) {
        configureInternalTlsPair(*internal_cluster, *internal_chain);
      }
    });
    HttpIntegrationTest::initialize();
  }

  void expectNoInternalTlsErrors() {
    EXPECT_EQ(0, test_server_->counter("cluster.cluster_internal.ssl.connection_error")->value());
    EXPECT_EQ(
        0, test_server_->counter("listener.envoy_internal_internal_listener.ssl.connection_error")
               ->value());
  }

  void setUpTunnel() {
    tcp_client_ = makeTcpConnection(lookupPort("real_listener"));
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
    upstream_request_->encodeHeaders(default_response_headers_, false);

    ASSERT_TRUE(tcp_client_->write("hello"));
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));
  }

  std::string real_access_log_path_;
  IntegrationTcpClientPtr tcp_client_;
  bool enable_internal_tls_{false};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, InternalListenerTunnelingCloseIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Upstream half-closes (shutdown(WR) only) while client stays open: internal connection stays open
// for client->upstream data until the client closes, then closes as Normal.
TEST_P(InternalListenerTunnelingCloseIntegrationTest, UpstreamEndStreamPreservesHalfClose) {
  initialize();
  setUpTunnel();
  upstream_request_->encodeData("world", true);
  tcp_client_->waitForData("world");
  tcp_client_->waitForHalfClose();

  ASSERT_TRUE(tcp_client_->write("tail", /*end_stream=*/true));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 9));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_THAT(waitForAccessLog(real_access_log_path_), Eq("Normal -"));
  test_server_->waitForGauge("listener.envoy_internal_internal_listener.downstream_cx_active",
                             Eq(0));
  test_server_->waitForGauge("cluster.cluster_internal.upstream_cx_active", Eq(0));
  tcp_client_->close();
}

// Deterministic 1-turn schedule: upstream sends end_stream + reset in a single dispatcher callback
// while client write side stays open. Both listeners must drain cleanly BEFORE
// tcp_client_->close().
TEST_P(InternalListenerTunnelingCloseIntegrationTest, UpstreamEndStreamAndResetOneTurnClientOpen) {
  initialize();
  setUpTunnel();
  upstream_request_->postToConnectionThread([this]() {
    upstream_request_->encodeData("world", true);
    upstream_request_->encodeResetStream();
  });

  tcp_client_->waitForData("world");
  tcp_client_->waitForHalfClose();
  EXPECT_THAT(waitForAccessLog(real_access_log_path_), Eq("Normal -"));
  test_server_->waitForGauge("listener.envoy_internal_internal_listener.downstream_cx_active",
                             Eq(0));
  test_server_->waitForGauge("cluster.cluster_internal.upstream_cx_active", Eq(0));
  tcp_client_->close();
}

// Deterministic 2-turn schedule: client observes EOF from turn 1 first, then upstream resets/closes
// on turn 2 while client write side is still open. Must not leak or reset.
TEST_P(InternalListenerTunnelingCloseIntegrationTest,
       UpstreamEndStreamThenLateResetTwoTurnClientOpen) {
  initialize();
  setUpTunnel();
  upstream_request_->encodeData("world", true);
  tcp_client_->waitForData("world");
  tcp_client_->waitForHalfClose();

  upstream_request_->encodeResetStream();
  EXPECT_THAT(waitForAccessLog(real_access_log_path_), Eq("Normal -"));
  test_server_->waitForGauge("listener.envoy_internal_internal_listener.downstream_cx_active",
                             Eq(0));
  test_server_->waitForGauge("cluster.cluster_internal.upstream_cx_active", Eq(0));
  tcp_client_->close();
}

TEST_P(InternalListenerTunnelingCloseIntegrationTest,
       UpstreamEndStreamAndResetUserSpaceRstDisabled) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.enable_send_rst_on_user_space_socket", "false");
  initialize();
  setUpTunnel();
  upstream_request_->postToConnectionThread([this]() {
    upstream_request_->encodeData("world", true);
    upstream_request_->encodeResetStream();
  });

  tcp_client_->waitForData("world");
  tcp_client_->waitForHalfClose();
  tcp_client_->close();
  EXPECT_THAT(waitForAccessLog(real_access_log_path_), Eq("Normal -"));
}

// TLS between the real listener's upstream connection and the internal listener (SslSocket on both
// ends of the user-space socket pair). SslSocket::doRead stops at close_notify
// (SSL_ERROR_ZERO_RETURN) without issuing an extra empty BIO_read on the user-space IoHandle, so
// the peer full close must be detected via IoHandle::wasPeerFullyClosed() rather than read().
TEST_P(InternalListenerTunnelingCloseIntegrationTest,
       TlsUpstreamEndStreamAndResetOneTurnClientOpen) {
  enable_internal_tls_ = true;
  initialize();
  setUpTunnel();
  upstream_request_->postToConnectionThread([this]() {
    upstream_request_->encodeData("world", true);
    upstream_request_->encodeResetStream();
  });

  tcp_client_->waitForData("world");
  tcp_client_->waitForHalfClose();
  EXPECT_THAT(waitForAccessLog(real_access_log_path_), Eq("Normal -"));
  test_server_->waitForGauge("listener.envoy_internal_internal_listener.downstream_cx_active",
                             Eq(0));
  test_server_->waitForGauge("cluster.cluster_internal.upstream_cx_active", Eq(0));
  expectNoInternalTlsErrors();
  tcp_client_->close();
}

TEST_P(InternalListenerTunnelingCloseIntegrationTest,
       TlsUpstreamEndStreamThenLateResetTwoTurnClientOpen) {
  enable_internal_tls_ = true;
  initialize();
  setUpTunnel();
  upstream_request_->encodeData("world", true);
  tcp_client_->waitForData("world");
  tcp_client_->waitForHalfClose();

  upstream_request_->encodeResetStream();
  EXPECT_THAT(waitForAccessLog(real_access_log_path_), Eq("Normal -"));
  test_server_->waitForGauge("listener.envoy_internal_internal_listener.downstream_cx_active",
                             Eq(0));
  test_server_->waitForGauge("cluster.cluster_internal.upstream_cx_active", Eq(0));
  expectNoInternalTlsErrors();
  tcp_client_->close();
}

// Outer HCM terminating HTTP/2 CONNECT (Http::Tcp::TcpUpstream) -> cluster_internal ->
// internal_listener (HTTP/1.1 HCM Connection: close, or H2 tunneling tcp_proxy).
class InternalListenerConnectTerminationIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  InternalListenerConnectTerminationIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam(), ConfigHelper::httpProxyConfig()) {
    enableHalfClose(true);
  }

  void initialize() override {
    if (internal_uses_h2_tunnel_) {
      setUpstreamProtocol(Http::CodecType::HTTP2);
    }
    useAccessLog("%RESPONSE_CODE% %RESPONSE_FLAGS% %UPSTREAM_DETECTED_CLOSE_TYPE%");
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* bootstrap_extension = bootstrap.add_bootstrap_extensions();
      bootstrap_extension->set_name("envoy.bootstrap.internal_listener");
      envoy::extensions::bootstrap::internal_listener::v3::InternalListener
          internal_listener_config;
      std::ignore = bootstrap_extension->mutable_typed_config()->PackFrom(internal_listener_config);

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

      // Clone listener 0's valid HCM before re-pointing listener 0 to terminate CONNECT into
      // cluster_internal.
      auto* outer_filter_blob = bootstrap.mutable_static_resources()
                                    ->mutable_listeners(0)
                                    ->mutable_filter_chains(0)
                                    ->mutable_filters(0)
                                    ->mutable_typed_config();
      auto outer_hcm = MessageUtil::anyConvert<
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager>(
          *outer_filter_blob);
      auto inner_hcm = outer_hcm;
      inner_hcm.set_stat_prefix("http_internal");
      inner_hcm.set_codec_type(envoy::extensions::filters::network::http_connection_manager::v3::
                                   HttpConnectionManager::HTTP1);
      inner_hcm.clear_access_log();

      outer_hcm.mutable_route_config()
          ->mutable_virtual_hosts(0)
          ->mutable_routes(0)
          ->mutable_route()
          ->set_cluster("cluster_internal");
      ConfigHelper::setConnectConfig(outer_hcm, /*terminate_connect=*/true, /*allow_post=*/false);
      std::ignore = outer_filter_blob->PackFrom(outer_hcm);

      auto* internal_listener = bootstrap.mutable_static_resources()->add_listeners();
      internal_listener->set_name("internal_listener");
      internal_listener->mutable_internal_listener();
      auto* internal_chain = internal_listener->add_filter_chains();
      auto* internal_filter = internal_chain->add_filters();
      if (internal_uses_h2_tunnel_) {
        internal_filter->set_name("tcp_proxy");
        envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy internal_tcp_proxy;
        internal_tcp_proxy.set_cluster("cluster_0");
        internal_tcp_proxy.set_stat_prefix("tcp_proxy_internal");
        internal_tcp_proxy.mutable_tunneling_config()->set_hostname("foo.lyft.com:80");
        std::ignore = internal_filter->mutable_typed_config()->PackFrom(internal_tcp_proxy);
      } else {
        internal_filter->set_name("http");
        std::ignore = internal_filter->mutable_typed_config()->PackFrom(inner_hcm);
      }

      if (enable_internal_tls_) {
        configureInternalTlsPair(*internal_cluster, *internal_chain);
      }
    });
    HttpIntegrationTest::initialize();
  }

  void runHttp1ConnectionCloseOverConnectTunnel() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder = codec_client_->startRequest(
        Http::TestRequestHeaderMapImpl{{":method", "CONNECT"}, {":authority", "foo.lyft.com:80"}});
    request_encoder_ = &encoder_decoder.first;
    IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
    response->waitForHeaders();
    EXPECT_EQ("200", response->headers().getStatusValue());

    // Send HTTP/1.1 request with Connection: close inside the open HTTP/2 CONNECT stream
    // (end_stream=false on the CONNECT stream itself).
    codec_client_->sendData(*request_encoder_,
                            "GET / HTTP/1.1\r\nHost: foo.lyft.com\r\nConnection: close\r\n\r\n",
                            /*end_stream=*/false);

    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

    upstream_request_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-length", "5"}}, false);
    upstream_request_->encodeData("world", true);

    // Must deliver full HTTP/1.1 payload WITH end_stream=true (not truncated by INTERNAL_ERROR).
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->body(), testing::HasSubstr("\r\n\r\nworld"));

    // Because the client request half is still open, RFC 9113 section 8.1 closes the stream via
    // RST_STREAM(NO_ERROR) (RemoteResetNoError), not INTERNAL_ERROR (RemoteReset).
    Event::TestTimeSystem::RealTimeBound bound(TestUtility::DefaultTimeout);
    while (!response->reset() && bound.withinBound()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    ASSERT_TRUE(response->reset());
    EXPECT_EQ(Http::StreamResetReason::RemoteResetNoError, response->resetReason());

    EXPECT_THAT(waitForAccessLog(access_log_name_), Eq("200 - Normal"));
    test_server_->waitForGauge("listener.envoy_internal_internal_listener.downstream_cx_active",
                               Eq(0));
    test_server_->waitForGauge("cluster.cluster_internal.upstream_cx_active", Eq(0));
    cleanupUpstreamAndDownstream();
  }

  bool enable_internal_tls_{false};
  bool internal_uses_h2_tunnel_{false};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, InternalListenerConnectTerminationIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(InternalListenerConnectTerminationIntegrationTest,
       InternalHttp1ConnectionCloseDeliversEndStreamAndNoErrorReset) {
  initialize();
  runHttp1ConnectionCloseOverConnectTunnel();
}

TEST_P(InternalListenerConnectTerminationIntegrationTest,
       InternalHttp1ConnectionCloseOverTlsDeliversEndStreamAndNoErrorReset) {
  enable_internal_tls_ = true;
  initialize();
  runHttp1ConnectionCloseOverConnectTunnel();
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_internal.ssl.connection_error")->value());
  EXPECT_EQ(0,
            test_server_->counter("listener.envoy_internal_internal_listener.ssl.connection_error")
                ->value());
}

TEST_P(InternalListenerConnectTerminationIntegrationTest,
       InternalHttp1ConnectionCloseWithUpstreamHalfCloseDisabled) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.tcp_tunnel_allow_upstream_half_close", "false");
  initialize();
  runHttp1ConnectionCloseOverConnectTunnel();
}

TEST_P(InternalListenerConnectTerminationIntegrationTest,
       TwoTurnUpstreamHalfCloseThenCloseThroughConnectTermination) {
  internal_uses_h2_tunnel_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "CONNECT"}, {":authority", "foo.lyft.com:80"}});
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);
  response->waitForHeaders();

  codec_client_->sendData(*request_encoder_, "hello", false);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

  // Turn 1: upstream half-closes. Client sees data + end_stream while tunnel stays half-open.
  upstream_request_->encodeData("world", true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("world", response->body());
  EXPECT_FALSE(response->reset());

  // Turn 2: upstream closes the tunneled stream. Internal listener closes user-space socket B;
  // outer 'HCM' closes the downstream H2 stream with 'NO_ERROR' and zero 'UC' flags.
  upstream_request_->encodeResetStream();
  Event::TestTimeSystem::RealTimeBound bound(TestUtility::DefaultTimeout);
  while (!response->reset() && bound.withinBound()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  ASSERT_TRUE(response->reset());
  EXPECT_EQ(Http::StreamResetReason::RemoteResetNoError, response->resetReason());

  EXPECT_THAT(waitForAccessLog(access_log_name_), Eq("200 - Normal"));
  test_server_->waitForGauge("listener.envoy_internal_internal_listener.downstream_cx_active",
                             Eq(0));
  test_server_->waitForGauge("cluster.cluster_internal.upstream_cx_active", Eq(0));
  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Envoy
