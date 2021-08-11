#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/accesslog/v3/als.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/common/version/version.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

class AccessLogIntegrationTest : public Grpc::VersionedGrpcClientIntegrationParamTest,
                                 public HttpIntegrationTest {
public:
  AccessLogIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initialize() override {
    if (apiVersion() != envoy::config::core::v3::ApiVersion::V3) {
      config_helper_.enableDeprecatedV2Api();
    }
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* accesslog_cluster = bootstrap.mutable_static_resources()->add_clusters();
      accesslog_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      accesslog_cluster->set_name("accesslog");
      ConfigHelper::setHttp2(*accesslog_cluster);
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
          common_config->set_transport_api_version(apiVersion());
          setGrpcService(*common_config->mutable_grpc_service(), "accesslog",
                         fake_upstreams_.back()->localAddress());
          access_log->mutable_typed_config()->PackFrom(config);
        });

    HttpIntegrationTest::initialize();
  }

  static ProtobufTypes::MessagePtr scrubHiddenEnvoyDeprecated(const Protobuf::Message& message) {
    ProtobufTypes::MessagePtr mutable_clone;
    mutable_clone.reset(message.New());
    mutable_clone->MergeFrom(message);
    Config::VersionUtil::scrubHiddenEnvoyDeprecated(*mutable_clone);
    return mutable_clone;
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
    Config::VersionUtil::scrubHiddenEnvoyDeprecated(request_msg);
    Config::VersionUtil::scrubHiddenEnvoyDeprecated(expected_request_msg);
    EXPECT_THAT(request_msg, ProtoEq(expected_request_msg));
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
                         VERSIONED_GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::VersionedGrpcClientIntegrationParamTest::protocolTestParamsToString);

// Test a basic full access logging flow.
TEST_P(AccessLogIntegrationTest, BasicAccessLogFlow) {
  XDS_DEPRECATED_FEATURE_TEST_SKIP;
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
    user_agent_name: "envoy"
  log_name: foo
http_logs:
  log_entry:
    common_properties:
      response_flags:
        no_route_found: true
    protocol_version: HTTP11
    request:
      scheme: http
      authority: host
      path: /notfound
      request_headers_bytes: 118
      request_method: GET
    response:
      response_code:
        value: 404
      response_code_details: "route_not_found"
      response_headers_bytes: 54
)EOF")));

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/notfound", "", downstream_protocol_, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
  ASSERT_TRUE(waitForAccessLogRequest(R"EOF(
http_logs:
  log_entry:
    common_properties:
      response_flags:
        no_route_found: true
    protocol_version: HTTP11
    request:
      scheme: http
      authority: host
      path: /notfound
      request_headers_bytes: 118
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
  EXPECT_EQ("404", response->headers().getStatusValue());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(waitForAccessLogRequest(fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
    user_agent_name: "envoy"
  log_name: foo
http_logs:
  log_entry:
    common_properties:
      response_flags:
        no_route_found: true
    protocol_version: HTTP11
    request:
      scheme: http
      authority: host
      path: /notfound
      request_headers_bytes: 118
      request_method: GET
    response:
      response_code:
        value: 404
      response_code_details: "route_not_found"
      response_headers_bytes: 54
)EOF")));
  cleanup();
}

} // namespace
} // namespace Envoy
