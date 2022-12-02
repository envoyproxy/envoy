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
using testing::HasSubstr;

namespace Envoy {
namespace {

class AccessLogIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                 public HttpIntegrationTest {
public:
  AccessLogIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, ipVersion()) {
    // TODO(ggreenway): add tag extraction rules.
    // Missing stat tag-extraction rule for stat 'grpc.accesslog.streams_closed_11' and stat_prefix
    // 'accesslog'.
    skip_tag_extraction_rule_check_ = true;
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initialize() override {
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
          common_config->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
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
    EXPECT_EQ("POST", access_log_request_->headers().getMethodValue());
    EXPECT_EQ("/envoy.service.accesslog.v3.AccessLogService/StreamAccessLogs",
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
    log_entry->mutable_common_properties()->clear_duration();
    log_entry->mutable_common_properties()->clear_time_to_last_rx_byte();
    log_entry->mutable_common_properties()->clear_time_to_first_downstream_tx_byte();
    log_entry->mutable_common_properties()->clear_time_to_last_downstream_tx_byte();
    log_entry->mutable_request()->clear_request_id();
    log_entry->mutable_common_properties()->clear_stream_id();
    if (request_msg.has_identifier()) {
      auto* node = request_msg.mutable_identifier()->mutable_node();
      node->clear_extensions();
      node->clear_user_agent_build_version();
    }
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
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);

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
      response_headers_bytes: 131
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
      response_headers_bytes: 131
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
    PANIC("reached unexpected code");
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
      response_headers_bytes: 131
)EOF")));
  cleanup();
}

// Regression test to make sure that configuring upstream logs over gRPC will not crash Envoy.
// TODO(asraa): Test output of the upstream logs.
// See https://github.com/envoyproxy/envoy/issues/8828.
TEST_P(AccessLogIntegrationTest, ConfigureHttpOverGrpcLogs) {
  setUpstreamProtocol(Http::CodecType::HTTP2);
  setDownstreamProtocol(Http::CodecType::HTTP2);
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        // Configure just enough of an upstream access log to reference the upstream headers.
        const std::string yaml_string = R"EOF(
name: router
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  upstream_log:
    name: grpc_accesslog
    filter:
      not_health_check_filter: {}
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.grpc.v3.HttpGrpcAccessLogConfig
      common_config:
        log_name: foo
        transport_api_version: V3
        grpc_service:
          envoy_grpc:
            cluster_name: cluster_0
  )EOF";
        // Replace the terminal envoy.router.
        hcm.clear_http_filters();
        TestUtility::loadFromYaml(yaml_string, *hcm.add_http_filters());
      });

  initialize();

  // Send the request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Send the response headers.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Verify the grpc cached logger is available after the initial logger filter is destroyed.
// Regression test for https://github.com/envoyproxy/envoy/issues/18066
TEST_P(AccessLogIntegrationTest, GrpcLoggerSurvivesAfterReloadConfig) {
#ifdef ENVOY_ENABLE_UHV
  // TODO(#23287) - Determine HTTP/0.9 and HTTP/1.0 support within UHV
  return;
#endif

  config_helper_.disableDelayClose();
  autonomous_upstream_ = true;
  // The grpc access logger connection never closes. It's ok to see an incomplete logging stream.
  autonomous_allow_incomplete_streams_ = true;

  const std::string grpc_logger_string = R"EOF(
    name: grpc_accesslog
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.grpc.v3.HttpGrpcAccessLogConfig
      common_config:
        log_name: bar
        transport_api_version: V3
        grpc_service:
          envoy_grpc:
            cluster_name: cluster_0
  )EOF";

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    listener->set_stat_prefix("listener_0");
  });
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) { TestUtility::loadFromYaml(grpc_logger_string, *hcm.add_access_log()); });
  initialize();
  // Given we're using LDS in this test, initialize() will not complete until
  // the initial LDS file has loaded.
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  // HTTP 1.1 is allowed and the connection is kept open until the listener update.
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.1\r\nHost: host\r\n\r\n",
                                &response, true);
  EXPECT_TRUE(response.find("HTTP/1.1 200") == 0);

  test_server_->waitForCounterEq("access_logs.grpc_access_log.logs_written", 2);

  // Create a new config with HTTP/1.0 proxying. The goal is to trigger a listener update.
  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        hcm.mutable_http_protocol_options()->set_accept_http_10(true);
        hcm.mutable_http_protocol_options()->set_default_host_for_http_10("default.com");
      });

  // Create an LDS response with the new config, and reload config.
  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForCounterEq("listener_manager.lds.update_success", 2);

  // Wait until the http 1.1 connection is destroyed due to the listener update. It indicates the
  // listener starts draining.
  test_server_->waitForGaugeEq("listener.listener_0.downstream_cx_active", 0);
  // Wait until all the draining filter chain is gone. It indicates the old listener and filter
  // chains are destroyed.
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 0);

  // Verify that the new listener config is applied.
  std::string response2;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response2, true);
  EXPECT_THAT(response2, HasSubstr("HTTP/1.0 200 OK\r\n"));

  // Verify that the grpc access logger is available after the listener update.
  test_server_->waitForCounterEq("access_logs.grpc_access_log.logs_written", 4);
}

} // namespace
} // namespace Envoy
