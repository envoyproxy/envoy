#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/accesslog/v3/als.pb.h"

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

class AccessLogIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                 public HttpIntegrationTest {
public:
  AccessLogIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_, timeSystem()));
  }

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* accesslog_cluster = bootstrap.mutable_static_resources()->add_clusters();
      accesslog_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      accesslog_cluster->set_name("accesslog");
      accesslog_cluster->mutable_http2_protocol_options();
    });

    config_helper_.addConfigModifier(
        [this](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* access_log = hcm.add_access_log();
          access_log->set_name("grpc_accesslog");

          envoy::extensions::access_loggers::grpc::v3::HttpGrpcAccessLogConfig config;
          auto* common_config = config.mutable_common_config();
          common_config->set_log_name("foo");
          setGrpcService(*common_config->mutable_grpc_service(), "accesslog",
                         fake_upstreams_.back()->localAddress());
          access_log->mutable_typed_config()->PackFrom(config);
        });

    HttpIntegrationTest::initialize();
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
    EXPECT_EQ("POST", access_log_request_->headers().Method()->value().getStringView());
    EXPECT_EQ("/envoy.service.accesslog.v2.AccessLogService/StreamAccessLogs",
              access_log_request_->headers().Path()->value().getStringView());
    EXPECT_EQ("application/grpc",
              access_log_request_->headers().ContentType()->value().getStringView());

    envoy::service::accesslog::v3::StreamAccessLogsMessage expected_request_msg;
    TestUtility::loadFromYaml(expected_request_msg_yaml, expected_request_msg);

    // Clear fields which are not deterministic.
    auto* log_entry = request_msg.mutable_http_logs()->mutable_log_entry(0);
    log_entry->mutable_common_properties()->clear_downstream_remote_address();
    log_entry->mutable_common_properties()->clear_downstream_direct_remote_address();
    log_entry->mutable_common_properties()->clear_downstream_local_address();
    log_entry->mutable_common_properties()->clear_start_time();
    log_entry->mutable_common_properties()->clear_time_to_last_rx_byte();
    log_entry->mutable_common_properties()->clear_time_to_first_downstream_tx_byte();
    log_entry->mutable_common_properties()->clear_time_to_last_downstream_tx_byte();
    log_entry->mutable_request()->clear_request_id();
    if (request_msg.has_identifier()) {
      auto* node = request_msg.mutable_identifier()->mutable_node();
      node->clear_extensions();
      node->clear_user_agent_build_version();
    }
    EXPECT_EQ(request_msg.DebugString(), expected_request_msg.DebugString());

    return AssertionSuccess();
  }

  void cleanup() {
    if (fake_access_log_connection_ != nullptr) {
      AssertionResult result = fake_access_log_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_access_log_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
  }

  FakeHttpConnectionPtr fake_access_log_connection_;
  FakeStreamPtr access_log_request_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsCientType, AccessLogIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// Test a basic full access logging flow.
TEST_P(AccessLogIntegrationTest, BasicAccessLogFlow) {
  testRouterNotFound();
  ASSERT_TRUE(waitForAccessLogConnection());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(waitForAccessLogRequest(fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
    build_version: {}
    user_agent_name: "envoy"
  log_name: foo
http_logs:
  log_entry:
    common_properties:
      response_flags:
        no_route_found: true
    protocol_version: HTTP11
    request:
      authority: host
      path: /notfound
      request_headers_bytes: 122
      request_method: GET
    response:
      response_code:
        value: 404
      response_code_details: "route_not_found"
      response_headers_bytes: 54
)EOF",
                                                  VersionInfo::version())));

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/notfound", "", downstream_protocol_, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().Status()->value().getStringView());
  ASSERT_TRUE(waitForAccessLogRequest(R"EOF(
http_logs:
  log_entry:
    common_properties:
      response_flags:
        no_route_found: true
    protocol_version: HTTP11
    request:
      authority: host
      path: /notfound
      request_headers_bytes: 122
      request_method: GET
    response:
      response_code:
        value: 404
      response_code_details: "route_not_found"
      response_headers_bytes: 54
)EOF"));

  // Send an empty response and end the stream. This should never happen but make sure nothing
  // breaks and we make a new stream on a follow up request.
  access_log_request_->startGrpcStream();
  envoy::service::accesslog::v3::StreamAccessLogsResponse response_msg;
  access_log_request_->sendGrpcMessage(response_msg);
  access_log_request_->finishGrpcStream(Grpc::Status::Ok);
  switch (clientType()) {
  case Grpc::ClientType::EnvoyGrpc:
    test_server_->waitForGaugeEq("cluster.accesslog.upstream_rq_active", 0);
    break;
  case Grpc::ClientType::GoogleGrpc:
    test_server_->waitForCounterGe("grpc.accesslog.streams_closed_0", 1);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  response = IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", "/notfound", "",
                                                downstream_protocol_, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().Status()->value().getStringView());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(waitForAccessLogRequest(fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
    build_version: {}
    user_agent_name: "envoy"
  log_name: foo
http_logs:
  log_entry:
    common_properties:
      response_flags:
        no_route_found: true
    protocol_version: HTTP11
    request:
      authority: host
      path: /notfound
      request_headers_bytes: 122
      request_method: GET
    response:
      response_code:
        value: 404
      response_code_details: "route_not_found"
      response_headers_bytes: 54
)EOF",
                                                  VersionInfo::version())));
  cleanup();
}

} // namespace
} // namespace Envoy
