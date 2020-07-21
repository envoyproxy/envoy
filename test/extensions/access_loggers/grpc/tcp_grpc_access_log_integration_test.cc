#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/service/accesslog/v3/als.pb.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "common/version/version.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

void clearPort(envoy::config::core::v3::Address& address) {
  address.mutable_socket_address()->clear_port_specifier();
}

class TcpGrpcAccessLogIntegrationTest : public Grpc::VersionedGrpcClientIntegrationParamTest,
                                        public BaseIntegrationTest {
public:
  TcpGrpcAccessLogIntegrationTest()
      : BaseIntegrationTest(ipVersion(), ConfigHelper::tcpProxyConfig()) {
    enable_half_close_ = true;
  }

  void createUpstreams() override {
    BaseIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_, timeSystem()));
  }

  void initialize() override {
    config_helper_.renameListener("tcp_proxy");
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* accesslog_cluster = bootstrap.mutable_static_resources()->add_clusters();
      accesslog_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      accesslog_cluster->set_name("accesslog");
      accesslog_cluster->mutable_http2_protocol_options();
    });

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* access_log = listener->add_access_log();
      access_log->set_name("grpc_accesslog");
      envoy::extensions::access_loggers::grpc::v3::TcpGrpcAccessLogConfig access_log_config;
      auto* common_config = access_log_config.mutable_common_config();
      common_config->set_log_name("foo");
      common_config->set_transport_api_version(apiVersion());
      setGrpcService(*common_config->mutable_grpc_service(), "accesslog",
                     fake_upstreams_.back()->localAddress());
      access_log->mutable_typed_config()->PackFrom(access_log_config);
    });
    BaseIntegrationTest::initialize();
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
    EXPECT_EQ(TestUtility::getVersionedMethodPath("envoy.service.accesslog.{}.AccessLogService",
                                                  "StreamAccessLogs", apiVersion()),
              access_log_request_->headers().getPathValue());
    EXPECT_EQ("application/grpc", access_log_request_->headers().getContentTypeValue());

    envoy::service::accesslog::v3::StreamAccessLogsMessage expected_request_msg;
    TestUtility::loadFromYaml(expected_request_msg_yaml, expected_request_msg);

    // Clear fields which are not deterministic.
    auto* log_entry = request_msg.mutable_tcp_logs()->mutable_log_entry(0);
    clearPort(*log_entry->mutable_common_properties()->mutable_downstream_remote_address());
    clearPort(*log_entry->mutable_common_properties()->mutable_downstream_direct_remote_address());
    clearPort(*log_entry->mutable_common_properties()->mutable_downstream_local_address());
    clearPort(*log_entry->mutable_common_properties()->mutable_upstream_remote_address());
    clearPort(*log_entry->mutable_common_properties()->mutable_upstream_local_address());
    log_entry->mutable_common_properties()->clear_start_time();
    log_entry->mutable_common_properties()->clear_time_to_last_rx_byte();
    log_entry->mutable_common_properties()->clear_time_to_first_downstream_tx_byte();
    log_entry->mutable_common_properties()->clear_time_to_last_downstream_tx_byte();
    if (request_msg.has_identifier()) {
      auto* node = request_msg.mutable_identifier()->mutable_node();
      node->clear_extensions();
      node->clear_user_agent_build_version();
    }
    Config::VersionUtil::scrubHiddenEnvoyDeprecated(request_msg);
    Config::VersionUtil::scrubHiddenEnvoyDeprecated(expected_request_msg);
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
};

INSTANTIATE_TEST_SUITE_P(IpVersionsCientType, TcpGrpcAccessLogIntegrationTest,
                         VERSIONED_GRPC_CLIENT_INTEGRATION_PARAMS);

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
  ASSERT_TRUE(waitForAccessLogRequest(
      fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
    build_version: {}
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
      downstream_direct_remote_address:
        socket_address:
          address: {}
    connection_properties:
      received_bytes: 3
      sent_bytes: 5
)EOF",
                  VersionInfo::version(), Network::Test::getLoopbackAddressString(ipVersion()),
                  Network::Test::getLoopbackAddressString(ipVersion()),
                  Network::Test::getLoopbackAddressString(ipVersion()),
                  Network::Test::getLoopbackAddressString(ipVersion()),
                  Network::Test::getLoopbackAddressString(ipVersion()))));

  cleanup();
}

} // namespace
} // namespace Envoy
