#include "envoy/config/accesslog/v2/als.pb.h"
#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.validate.h"
#include "envoy/service/accesslog/v2/als.pb.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/version.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

void clearPort(envoy::api::v2::core::Address& address) {
  address.mutable_socket_address()->clear_port_specifier();
}

class TcpGrpcAccessLogIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                        public BaseIntegrationTest {
public:
  TcpGrpcAccessLogIntegrationTest()
      : BaseIntegrationTest(ipVersion(), ConfigHelper::TCP_PROXY_CONFIG) {
    enable_half_close_ = true;
  }

  ~TcpGrpcAccessLogIntegrationTest() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void createUpstreams() override {
    BaseIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_, timeSystem()));
  }

  void initialize() override {
    config_helper_.renameListener("tcp_proxy");
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* accesslog_cluster = bootstrap.mutable_static_resources()->add_clusters();
      accesslog_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      accesslog_cluster->set_name("accesslog");
      accesslog_cluster->mutable_http2_protocol_options();
    });

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);
      auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

      ASSERT_TRUE(config_blob->Is<envoy::config::filter::network::tcp_proxy::v2::TcpProxy>());
      auto tcp_proxy_config =
          MessageUtil::anyConvert<envoy::config::filter::network::tcp_proxy::v2::TcpProxy>(
              *config_blob);

      auto* access_log = tcp_proxy_config.add_access_log();
      access_log->set_name("envoy.tcp_grpc_access_log");
      envoy::config::accesslog::v2::TcpGrpcAccessLogConfig access_log_config;
      auto* common_config = access_log_config.mutable_common_config();
      common_config->set_log_name("foo");
      setGrpcService(*common_config->mutable_grpc_service(), "accesslog",
                     fake_upstreams_.back()->localAddress());
      access_log->mutable_typed_config()->PackFrom(access_log_config);
      config_blob->PackFrom(tcp_proxy_config);
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
    envoy::service::accesslog::v2::StreamAccessLogsMessage request_msg;
    VERIFY_ASSERTION(access_log_request_->waitForGrpcMessage(*dispatcher_, request_msg));
    EXPECT_EQ("POST", access_log_request_->headers().Method()->value().getStringView());
    EXPECT_EQ("/envoy.service.accesslog.v2.AccessLogService/StreamAccessLogs",
              access_log_request_->headers().Path()->value().getStringView());
    EXPECT_EQ("application/grpc",
              access_log_request_->headers().ContentType()->value().getStringView());

    envoy::service::accesslog::v2::StreamAccessLogsMessage expected_request_msg;
    TestUtility::loadFromYaml(expected_request_msg_yaml, expected_request_msg);

    // Clear fields which are not deterministic.
    auto* log_entry = request_msg.mutable_tcp_logs()->mutable_log_entry(0);
    clearPort(*log_entry->mutable_common_properties()->mutable_downstream_remote_address());
    clearPort(*log_entry->mutable_common_properties()->mutable_downstream_local_address());
    clearPort(*log_entry->mutable_common_properties()->mutable_upstream_remote_address());
    clearPort(*log_entry->mutable_common_properties()->mutable_upstream_local_address());
    log_entry->mutable_common_properties()->clear_start_time();
    log_entry->mutable_common_properties()->clear_time_to_last_rx_byte();
    log_entry->mutable_common_properties()->clear_time_to_first_downstream_tx_byte();
    log_entry->mutable_common_properties()->clear_time_to_last_downstream_tx_byte();
    EXPECT_EQ(request_msg.DebugString(), expected_request_msg.DebugString());

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
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// Test a basic full access logging flow.
TEST_P(TcpGrpcAccessLogIntegrationTest, BasicAccessLogFlow) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->write("hello"));
  tcp_client->waitForData("hello");
  tcp_client->write("bar", false);

  ASSERT_TRUE(fake_upstream_connection->write("", true));
  tcp_client->waitForHalfClose();
  tcp_client->write("", true);
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
    connection_properties:
      received_bytes: 3
      sent_bytes: 5
)EOF",
                  VersionInfo::version(), Network::Test::getLoopbackAddressString(ipVersion()),
                  Network::Test::getLoopbackAddressString(ipVersion()),
                  Network::Test::getLoopbackAddressString(ipVersion()),
                  Network::Test::getLoopbackAddressString(ipVersion()))));

  cleanup();
}

} // namespace
} // namespace Envoy
