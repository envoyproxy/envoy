#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.validate.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/service/accesslog/v3/als.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/common/version/version.h"
#include "source/extensions/filters/listener/tls_inspector/tls_inspector.h"
#include "source/extensions/filters/network/rbac/config.h"
#include "source/extensions/transport_sockets/tls/context_manager_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

void clearPort(envoy::config::core::v3::Address& address) {
  address.mutable_socket_address()->clear_port_specifier();
}

void clearMultipleSubjectAltNames(
    envoy::data::accesslog::v3::TLSProperties_CertificateProperties& certificate_properties) {
  for (int i = 1; i < certificate_properties.subject_alt_name_size(); i++) {
    certificate_properties.mutable_subject_alt_name()->RemoveLast();
  }
}

class TcpGrpcAccessLogIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                        public BaseIntegrationTest {
public:
  TcpGrpcAccessLogIntegrationTest()
      : BaseIntegrationTest(ipVersion(), ConfigHelper::tcpProxyConfig()) {
    // TODO(ggreenway): add tag extraction rules.
    // Missing stat tag-extraction rule for stat 'grpc.accesslog.streams_closed_14' and stat_prefix
    // 'accesslog'.
    skip_tag_extraction_rule_check_ = true;

    enableHalfClose(true);
  }

  void createUpstreams() override {
    BaseIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initialize() override {
    config_helper_.renameListener("tcp_proxy");
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* accesslog_cluster = bootstrap.mutable_static_resources()->add_clusters();
      accesslog_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      accesslog_cluster->set_name("accesslog");
      ConfigHelper::setHttp2(*accesslog_cluster);
    });

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* access_log = listener->add_access_log();
      access_log->set_name("grpc_accesslog");
      envoy::extensions::access_loggers::grpc::v3::TcpGrpcAccessLogConfig access_log_config;
      auto* common_config = access_log_config.mutable_common_config();
      common_config->set_log_name("foo");
      common_config->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
      setGrpcService(*common_config->mutable_grpc_service(), "accesslog",
                     fake_upstreams_.back()->localAddress());
      access_log->mutable_typed_config()->PackFrom(access_log_config);
    });
    BaseIntegrationTest::initialize();
  }

  void initializeWithIntermediateLog() {
    config_helper_.renameListener("tcp_proxy");
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* accesslog_cluster = bootstrap.mutable_static_resources()->add_clusters();
      accesslog_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      accesslog_cluster->set_name("accesslog");
      ConfigHelper::setHttp2(*accesslog_cluster);
    });

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* tcp_proxy =
          listener->mutable_filter_chains(0)->mutable_filters(0)->mutable_typed_config();

      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy_config;
      tcp_proxy->UnpackTo(&tcp_proxy_config);

      auto* access_log = tcp_proxy_config.add_access_log();
      access_log->set_name("grpc_accesslog");
      envoy::extensions::access_loggers::grpc::v3::TcpGrpcAccessLogConfig access_log_config;
      auto* common_config = access_log_config.mutable_common_config();
      common_config->set_log_name("foo");
      common_config->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
      setGrpcService(*common_config->mutable_grpc_service(), "accesslog",
                     fake_upstreams_.back()->localAddress());
      access_log->mutable_typed_config()->PackFrom(access_log_config);

      tcp_proxy_config.mutable_access_log_flush_interval()->set_seconds(1); // 1s

      tcp_proxy->PackFrom(tcp_proxy_config);
    });
    BaseIntegrationTest::initialize();
  }

  void setupTlsInspectorFilter(bool ssl_terminate, bool enable_ja3_fingerprinting = false) {
    std::string tls_inspector_config = ConfigHelper::tlsInspectorFilter(enable_ja3_fingerprinting);
    config_helper_.addListenerFilter(tls_inspector_config);
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* filter_chain =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      auto* alpn = filter_chain->mutable_filter_chain_match()->add_application_protocols();
      *alpn = "envoyalpn";
      auto* filter = filter_chain->mutable_filters(0);
      filter->set_name("envoy.filters.network.echo");
      filter->clear_typed_config();
    });
    if (ssl_terminate) {
      config_helper_.addSslConfig();
    }
  }

  void setupSslConnection(bool expect_connection_open,
                          const Ssl::ClientSslTransportOptions& ssl_options = {},
                          const std::string& curves_list = "") {
    // Set up the SSL client.
    context_manager_ =
        std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());
    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("tcp_proxy"));
    context_ = Ssl::createClientSslTransportSocketFactory(ssl_options, *context_manager_, *api_);
    Network::TransportSocketPtr transport_socket;
    transport_socket =
        context_->createTransportSocket(std::make_shared<Network::TransportSocketOptionsImpl>(
                                            absl::string_view(""), std::vector<std::string>(),
                                            std::vector<std::string>{"envoyalpn"}),
                                        nullptr);
    if (!curves_list.empty()) {
      auto ssl_socket =
          dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
      ASSERT(ssl_socket != nullptr);
      SSL_set1_curves_list(ssl_socket->rawSslForTest(), curves_list.c_str());
    }
    client_ =
        dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                            std::move(transport_socket), nullptr, nullptr);
    client_->addConnectionCallbacks(connect_callbacks_);
    client_->connect();
    while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }

    if (expect_connection_open) {
      ;
      ASSERT(connect_callbacks_.connected());
      ASSERT_FALSE(connect_callbacks_.closed());
    } else {
      ASSERT_FALSE(connect_callbacks_.connected());
      ASSERT(connect_callbacks_.closed());
    }
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForAccessLogConnection() {
    return fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_access_log_connection_);
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForAccessLogStream() {
    return fake_access_log_connection_->waitForNewStream(*dispatcher_, access_log_request_);
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForAccessLogRequest(const std::string& expected_request_msg_yaml) {
    envoy::service::accesslog::v3::StreamAccessLogsMessage request_msg;
    VERIFY_ASSERTION(access_log_request_->waitForGrpcMessage(*dispatcher_, request_msg));
    EXPECT_EQ("POST", access_log_request_->headers().getMethodValue());
    EXPECT_EQ("/envoy.service.accesslog.v3.AccessLogService/StreamAccessLogs",
              access_log_request_->headers().getPathValue());
    EXPECT_EQ("application/grpc", access_log_request_->headers().getContentTypeValue());

    envoy::service::accesslog::v3::StreamAccessLogsMessage expected_request_msg;
    TestUtility::loadFromYaml(expected_request_msg_yaml, expected_request_msg);

    // Clean up possible redundant intermediate log entries
    request_msg.mutable_tcp_logs()->mutable_log_entry()->DeleteSubrange(
        1, request_msg.tcp_logs().log_entry_size() - 1);

    // Clear fields which are not deterministic.
    auto* log_entry = request_msg.mutable_tcp_logs()->mutable_log_entry(0);
    clearPort(*log_entry->mutable_common_properties()->mutable_downstream_remote_address());
    clearPort(*log_entry->mutable_common_properties()->mutable_downstream_direct_remote_address());
    clearPort(*log_entry->mutable_common_properties()->mutable_downstream_local_address());
    clearPort(*log_entry->mutable_common_properties()->mutable_upstream_remote_address());
    clearPort(*log_entry->mutable_common_properties()->mutable_upstream_local_address());
    log_entry->mutable_common_properties()->clear_start_time();
    log_entry->mutable_common_properties()->clear_duration();
    log_entry->mutable_common_properties()->clear_time_to_last_rx_byte();
    log_entry->mutable_common_properties()->clear_time_to_first_downstream_tx_byte();
    log_entry->mutable_common_properties()->clear_time_to_last_downstream_tx_byte();
    if (request_msg.has_identifier()) {
      auto* node = request_msg.mutable_identifier()->mutable_node();
      node->clear_extensions();
      node->clear_user_agent_build_version();
    }
    if (log_entry->mutable_common_properties()
            ->mutable_tls_properties()
            ->mutable_local_certificate_properties()
            ->subject_alt_name_size() > 1) {
      clearMultipleSubjectAltNames(*log_entry->mutable_common_properties()
                                        ->mutable_tls_properties()
                                        ->mutable_local_certificate_properties());
    }
    if (log_entry->mutable_common_properties()
            ->mutable_tls_properties()
            ->mutable_peer_certificate_properties()
            ->subject_alt_name_size() > 1) {
      clearMultipleSubjectAltNames(*log_entry->mutable_common_properties()
                                        ->mutable_tls_properties()
                                        ->mutable_peer_certificate_properties());
    }

    // Clear connection unique id which is not deterministic.
    log_entry->mutable_common_properties()->clear_stream_id();

    EXPECT_TRUE(TestUtility::protoEqual(request_msg, expected_request_msg,
                                        /*ignore_repeated_field_ordering=*/false));

    return AssertionSuccess();
  }

  void cleanup() {
    if (fake_access_log_connection_ != nullptr) {
      AssertionResult result = fake_access_log_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_access_log_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      fake_access_log_connection_ = nullptr;
    }
  }

  FakeHttpConnectionPtr fake_access_log_connection_;
  FakeStreamPtr access_log_request_;
  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::UpstreamTransportSocketFactoryPtr context_;
  ConnectionStatusCallbacks connect_callbacks_;
  Network::ClientConnectionPtr client_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsCientType, TcpGrpcAccessLogIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);

// Test a basic full access logging flow.
TEST_P(TcpGrpcAccessLogIntegrationTest, BasicAccessLogFlow) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->write("hello"));
  tcp_client->waitForData("hello");
  ASSERT_TRUE(tcp_client->write("bar", false));

  ASSERT_TRUE(fake_upstream_connection->write("", true));
  tcp_client->waitForHalfClose();
  ASSERT_TRUE(tcp_client->write("", true));

  ASSERT_TRUE(fake_upstream_connection->waitForData(3));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  ASSERT_TRUE(waitForAccessLogConnection());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(
      waitForAccessLogRequest(fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
    user_agent_name: "envoy"
  log_name: foo
tcp_logs:
  log_entry:
    common_properties:
      downstream_remote_address:
        socket_address:
          address: {}
      downstream_local_address:
        socket_address:
          address: {}
      upstream_remote_address:
        socket_address:
          address: {}
      upstream_local_address:
        socket_address:
          address: {}
      upstream_cluster: cluster_0
      upstream_request_attempt_count: 1
      downstream_direct_remote_address:
        socket_address:
          address: {}
    connection_properties:
      received_bytes: 3
      sent_bytes: 5
)EOF",
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()))));

  cleanup();
}

// Test a basic full access logging flow with intermediate log.
TEST_P(TcpGrpcAccessLogIntegrationTest, BasicAccessLogFlowWithIntermediateLog) {
  initializeWithIntermediateLog();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->write("hello"));
  tcp_client->waitForData("hello");
  ASSERT_TRUE(tcp_client->write("bar", false));

  ASSERT_TRUE(waitForAccessLogConnection());
  ASSERT_TRUE(waitForAccessLogStream());

  ASSERT_TRUE(
      waitForAccessLogRequest(fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
    user_agent_name: "envoy"
  log_name: foo
tcp_logs:
  log_entry:
    common_properties:
      downstream_remote_address:
        socket_address:
          address: {}
      downstream_local_address:
        socket_address:
          address: {}
      upstream_remote_address:
        socket_address:
          address: {}
      upstream_local_address:
        socket_address:
          address: {}
      upstream_cluster: cluster_0
      upstream_request_attempt_count: 1
      downstream_direct_remote_address:
        socket_address:
          address: {}
      intermediate_log_entry: true
    connection_properties:
      received_bytes: 3
      sent_bytes: 5
)EOF",
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()))));

  ASSERT_TRUE(fake_upstream_connection->write("", true));
  tcp_client->waitForHalfClose();
  ASSERT_TRUE(tcp_client->write("", true));

  ASSERT_TRUE(fake_upstream_connection->waitForData(3));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  cleanup();
}

// Test RBAC.
TEST_P(TcpGrpcAccessLogIntegrationTest, RBACAccessLogFlow) {
  config_helper_.addNetworkFilter(R"EOF(
name: envoy.filters.network.rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC
  stat_prefix: tcp.
  rules:
    action: ALLOW
    policies:
      "deny_all":
        permissions:
          - any: true
        principals:
          - not_id:
              any: true
)EOF");
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->write("hello"));
  tcp_client->waitForData("hello");
  ASSERT_TRUE(tcp_client->write("bar", false));

  ASSERT_TRUE(fake_upstream_connection->write("", true));
  tcp_client->waitForHalfClose();
  ASSERT_TRUE(tcp_client->write("", true));

  // Connection blocked. Do not wait for data, but directly wait for disconnect.
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  ASSERT_TRUE(waitForAccessLogConnection());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(
      waitForAccessLogRequest(fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
    user_agent_name: "envoy"
  log_name: foo
tcp_logs:
  log_entry:
    common_properties:
      downstream_remote_address:
        socket_address:
          address: {}
      downstream_local_address:
        socket_address:
          address: {}
      upstream_remote_address:
        socket_address:
          address: {}
      upstream_local_address:
        socket_address:
          address: {}
      upstream_cluster: cluster_0
      upstream_request_attempt_count: 1
      connection_termination_details: rbac_access_denied_matched_policy[none]
      downstream_direct_remote_address:
        socket_address:
          address: {}
    connection_properties:
      received_bytes: 3
      sent_bytes: 5
)EOF",
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()))));

  cleanup();
}

// Ssl Terminated by Envoy, no `ja3` fingerprint.
TEST_P(TcpGrpcAccessLogIntegrationTest, SslTerminatedNoJA3) {
  setupTlsInspectorFilter(/*ssl_terminate=*/true,
                          /*enable_`ja3`_fingerprinting=*/false);
  initialize();
  Ssl::ClientSslTransportOptions ssl_options;
  ssl_options.setSni("sni");
  ssl_options.setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
  ssl_options.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  setupSslConnection(/*expect_connection_open=*/true,
                     /*ssl_options=*/ssl_options, /*curves_list=*/"P-256");
  client_->close(Network::ConnectionCloseType::NoFlush);
  ASSERT_TRUE(waitForAccessLogConnection());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(
      waitForAccessLogRequest(fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
    user_agent_name: "envoy"
  log_name: foo
tcp_logs:
  log_entry:
    common_properties:
      downstream_remote_address:
        socket_address:
          address: {}
      downstream_local_address:
        socket_address:
          address: {}
      tls_properties:
        tls_version: TLSv1_2
        tls_cipher_suite:
          value: 49199
        tls_sni_hostname: sni
        local_certificate_properties:
          subject_alt_name:
            uri: "spiffe://lyft.com/backend-team"
          subject: "emailAddress=backend-team@lyft.com,CN=Test Backend Team,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US"
        peer_certificate_properties:
          subject_alt_name:
            uri: "spiffe://lyft.com/frontend-team"
          subject: "emailAddress=frontend-team@lyft.com,CN=Test Frontend Team,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US"
      upstream_remote_address:
        socket_address:
      upstream_local_address:
        socket_address:
      downstream_direct_remote_address:
        socket_address:
          address: {}
    connection_properties:
)EOF",
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()))));

  cleanup();
}

// Ssl Terminated by envoy, with `ja3` fingerprint.
TEST_P(TcpGrpcAccessLogIntegrationTest, SslTerminatedWithJA3) {
  setupTlsInspectorFilter(/*ssl_terminate=*/true,
                          /*enable_`ja3`_fingerprinting=*/true);
  initialize();
  Ssl::ClientSslTransportOptions ssl_options;
  ssl_options.setSni("sni");
  ssl_options.setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
  ssl_options.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  setupSslConnection(/*expect_connection_open=*/true,
                     /*ssl_options=*/ssl_options, /*curves_list=*/"P-256");
  client_->close(Network::ConnectionCloseType::NoFlush);
  ASSERT_TRUE(waitForAccessLogConnection());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(
      waitForAccessLogRequest(fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
    user_agent_name: "envoy"
  log_name: foo
tcp_logs:
  log_entry:
    common_properties:
      downstream_remote_address:
        socket_address:
          address: {}
      downstream_local_address:
        socket_address:
          address: {}
      tls_properties:
        tls_version: TLSv1_2
        tls_cipher_suite:
          value: 49199
        tls_sni_hostname: sni
        ja3_fingerprint: "ecaf91d232e224038f510cb81aa08b94"
        local_certificate_properties:
          subject_alt_name:
            uri: "spiffe://lyft.com/backend-team"
          subject: "emailAddress=backend-team@lyft.com,CN=Test Backend Team,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US"
        peer_certificate_properties:
          subject_alt_name:
            uri: "spiffe://lyft.com/frontend-team"
          subject: "emailAddress=frontend-team@lyft.com,CN=Test Frontend Team,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US"
      upstream_remote_address:
        socket_address:
      upstream_local_address:
        socket_address:
      downstream_direct_remote_address:
        socket_address:
          address: {}
    connection_properties:
)EOF",
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()))));

  cleanup();
}

// Ssl NOT Terminated by envoy, no `ja3` fingerprint.
TEST_P(TcpGrpcAccessLogIntegrationTest, SslNotTerminated) {
  setupTlsInspectorFilter(/*ssl_terminate=*/false,
                          /*enable_`ja3`_fingerprinting=*/false);
  initialize();
  Ssl::ClientSslTransportOptions ssl_options;
  ssl_options.setSni("sni");
  ssl_options.setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
  ssl_options.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  setupSslConnection(/*expect_connection_open=*/false,
                     /*ssl_options=*/ssl_options, /*curves_list=*/"P-256");
  client_->close(Network::ConnectionCloseType::NoFlush);
  ASSERT_TRUE(waitForAccessLogConnection());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(
      waitForAccessLogRequest(fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
    user_agent_name: "envoy"
  log_name: foo
tcp_logs:
  log_entry:
    common_properties:
      downstream_remote_address:
        socket_address:
          address: {}
      downstream_local_address:
        socket_address:
          address: {}
      upstream_remote_address:
        socket_address:
      upstream_local_address:
        socket_address:
      downstream_direct_remote_address:
        socket_address:
          address: {}
      tls_properties:
        tls_sni_hostname: sni
    connection_properties:
      received_bytes: 138
      sent_bytes: 138
)EOF",
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()))));

  cleanup();
}

// Ssl NOT Terminated by envoy, with `ja3` fingerprint.
TEST_P(TcpGrpcAccessLogIntegrationTest, SslNotTerminatedWithJA3) {
  setupTlsInspectorFilter(/*ssl_terminate=*/false,
                          /*enable_`ja3`_fingerprinting=*/true);
  initialize();
  Ssl::ClientSslTransportOptions ssl_options;
  ssl_options.setSni("sni");
  ssl_options.setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
  ssl_options.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  setupSslConnection(/*expect_connection_open=*/false,
                     /*ssl_options=*/ssl_options, /*curves_list=*/"P-256");
  client_->close(Network::ConnectionCloseType::NoFlush);
  ASSERT_TRUE(waitForAccessLogConnection());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(
      waitForAccessLogRequest(fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
    user_agent_name: "envoy"
  log_name: foo
tcp_logs:
  log_entry:
    common_properties:
      downstream_remote_address:
        socket_address:
          address: {}
      downstream_local_address:
        socket_address:
          address: {}
      upstream_remote_address:
        socket_address:
      upstream_local_address:
        socket_address:
      downstream_direct_remote_address:
        socket_address:
          address: {}
      tls_properties:
        tls_sni_hostname: sni
        ja3_fingerprint: "ecaf91d232e224038f510cb81aa08b94"
    connection_properties:
      received_bytes: 138
      sent_bytes: 138
)EOF",
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()))));

  cleanup();
}

// Ssl NOT Terminated by envoy, with only `ja3` fingerprint. No sni.
TEST_P(TcpGrpcAccessLogIntegrationTest, SslNotTerminatedWithJA3NoSNI) {
  setupTlsInspectorFilter(/*ssl_terminate=*/false,
                          /*enable_`ja3`_fingerprinting=*/true);
  initialize();
  Ssl::ClientSslTransportOptions ssl_options;
  ssl_options.setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
  ssl_options.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  setupSslConnection(/*expect_connection_open=*/false,
                     /*ssl_options=*/ssl_options, /*curves_list=*/"P-256");
  client_->close(Network::ConnectionCloseType::NoFlush);
  ASSERT_TRUE(waitForAccessLogConnection());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(
      waitForAccessLogRequest(fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
    user_agent_name: "envoy"
  log_name: foo
tcp_logs:
  log_entry:
    common_properties:
      downstream_remote_address:
        socket_address:
          address: {}
      downstream_local_address:
        socket_address:
          address: {}
      upstream_remote_address:
        socket_address:
      upstream_local_address:
        socket_address:
      downstream_direct_remote_address:
        socket_address:
          address: {}
      tls_properties:
        ja3_fingerprint: "71d1f47d1125ac53c3c6a4863c087cfe"
    connection_properties:
      received_bytes: 126
      sent_bytes: 126
)EOF",
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()),
                                          Network::Test::getLoopbackAddressString(ipVersion()))));

  cleanup();
}

} // namespace
} // namespace Envoy
