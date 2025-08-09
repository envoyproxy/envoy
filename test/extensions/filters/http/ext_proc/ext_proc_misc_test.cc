#include <algorithm>
#include <iostream>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/network/address.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/extensions/filters/http/ext_proc/config.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/integration/http_integration.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::service::ext_proc::v3::BodyResponse;
using envoy::service::ext_proc::v3::HeadersResponse;
using envoy::service::ext_proc::v3::HttpBody;
using envoy::service::ext_proc::v3::HttpHeaders;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;
using Http::LowerCaseString;

class ExtProcMiscIntegrationTest : public HttpIntegrationTest,
                                   public Grpc::GrpcClientIntegrationParamTest {
protected:
  ExtProcMiscIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();

    // Create separate "upstreams" for ExtProc gRPC servers
    for (int i = 0; i < grpc_upstream_count_; ++i) {
      grpc_upstreams_.push_back(&addFakeUpstream(http_codec_type_));
    }
  }

  void TearDown() override {
    if (processor_connection_) {
      ASSERT_TRUE(processor_connection_->close());
      ASSERT_TRUE(processor_connection_->waitForDisconnect());
    }
    cleanupUpstreamAndDownstream();
  }

  void initializeConfig(const std::vector<std::pair<int, int>>& cluster_endpoints = {{0, 1},
                                                                                     {1, 1}}) {
    int total_cluster_endpoints = 0;
    std::for_each(
        cluster_endpoints.begin(), cluster_endpoints.end(),
        [&total_cluster_endpoints](const auto& item) { total_cluster_endpoints += item.second; });
    ASSERT_EQ(total_cluster_endpoints, grpc_upstream_count_);

    config_helper_.addConfigModifier([this, cluster_endpoints](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      for (const auto& [cluster_id, endpoint_count] : cluster_endpoints) {
        auto* server_cluster = bootstrap.mutable_static_resources()->add_clusters();
        server_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        std::string cluster_name = absl::StrCat("ext_proc_server_", cluster_id);
        server_cluster->set_name(cluster_name);
        server_cluster->mutable_load_assignment()->set_cluster_name(cluster_name);
        ASSERT_EQ(server_cluster->load_assignment().endpoints_size(), 1);
        auto* endpoints = server_cluster->mutable_load_assignment()->mutable_endpoints(0);
        ASSERT_EQ(endpoints->lb_endpoints_size(), 1);
        for (int i = 1; i < endpoint_count; ++i) {
          auto* new_lb_endpoint = endpoints->add_lb_endpoints();
          new_lb_endpoint->MergeFrom(endpoints->lb_endpoints(0));
        }
      }

      const std::string valid_grpc_cluster_name = "ext_proc_server_0";
      setGrpcService(*proto_config_.mutable_grpc_service(), valid_grpc_cluster_name,
                     grpc_upstreams_[0]->localAddress());

      std::string ext_proc_filter_name = "envoy.filters.http.ext_proc";
      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter ext_proc_filter;
      ext_proc_filter.set_name(ext_proc_filter_name);
      ext_proc_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_proc_filter));
    });

    setUpstreamProtocol(Http::CodecType::HTTP1);
    setDownstreamProtocol(Http::CodecType::HTTP1);
  }

  IntegrationStreamDecoderPtr sendDownstreamRequest(
      absl::optional<std::function<void(Http::RequestHeaderMap& headers)>> modify_headers) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    if (modify_headers) {
      (*modify_headers)(headers);
    }
    return codec_client_->makeHeaderOnlyRequest(headers);
  }

  void processRequestHeadersMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(request.has_request_headers());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* headers = response.mutable_request_headers();
    const bool sendReply = !cb || (*cb)(request.request_headers(), *headers);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void processResponseHeadersMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(request.has_response_headers());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* headers = response.mutable_response_headers();
    const bool sendReply = !cb || (*cb)(request.response_headers(), *headers);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void processRequestBodyMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpBody&, BodyResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(request.has_request_body());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }

    // Send back the response from ext_proc server.
    ProcessingResponse response;
    auto* body = response.mutable_request_body();
    const bool sendReply = !cb || (*cb)(request.request_body(), *body);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void verifyDownstreamResponse(IntegrationStreamDecoder& response, int status_code) {
    ASSERT_TRUE(response.waitForEndStream());
    EXPECT_TRUE(response.complete());
    EXPECT_EQ(std::to_string(status_code), response.headers().getStatusValue());
  }

  bool IsEnvoyGrpc() { return std::get<1>(GetParam()) == Envoy::Grpc::ClientType::EnvoyGrpc; }

  void websocketExtProcTest() {
    if (!IsEnvoyGrpc()) {
      return;
    }

    http_codec_type_ = Http::CodecType::HTTP1;
    auto* forward_rules = proto_config_.mutable_forward_rules();
    auto* allowed_headers = forward_rules->mutable_allowed_headers();
    allowed_headers->add_patterns()->set_exact("upgrade");
    allowed_headers->add_patterns()->set_exact("connection");

    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) { hcm.add_upgrade_configs()->set_upgrade_type("websocket"); });

    const std::string local_reply_yaml = R"EOF(
body_format:
  json_format:
    code: "%RESPONSE_CODE%"
    message: "%LOCAL_REPLY_BODY%"
  )EOF";
    envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig
        local_reply_config;
    TestUtility::loadFromYaml(local_reply_yaml, local_reply_config);
    config_helper_.setLocalReply(local_reply_config);

    initializeConfig();
    HttpIntegrationTest::initialize();

    auto response = sendDownstreamRequest([](Http::HeaderMap& headers) {
      headers.addCopy(LowerCaseString("upgrade"), "websocket");
      headers.addCopy(LowerCaseString("connection"), "Upgrade");
    });

    processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);

    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.skip_ext_proc_on_local_reply")) {
      processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
    }
    verifyDownstreamResponse(*response, 200);
  }

  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config_{};
  std::vector<FakeUpstream*> grpc_upstreams_;
  FakeHttpConnectionPtr processor_connection_;
  FakeStreamPtr processor_stream_;
  TestScopedRuntime scoped_runtime_;
  int grpc_upstream_count_ = 2;
  Http::CodecType http_codec_type_ = Http::CodecType::HTTP2;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDeferredProcessing, ExtProcMiscIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// Test sending empty last body chunk with end_of_stream = true.
TEST_P(ExtProcMiscIntegrationTest, SendEmptyLastBodyChunk) {
  if (IsEnvoyGrpc()) {
    return;
  }

  proto_config_.mutable_message_timeout()->set_seconds(2);
  config_helper_.setBufferLimits(1024 * 1024, 1024 * 1024);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);

  // Config retry policy.
  config_helper_.addConfigModifier([](ConfigHelper::HttpConnectionManager& hcm) {
    auto* vhost = hcm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
    auto* retry_policy = vhost->mutable_retry_policy();
    retry_policy->set_retry_on("connect-failure");
    retry_policy->mutable_num_retries()->set_value(2);
  });
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.setMethod("POST");

  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  const uint32_t init_body_size = 1000;
  std::string init_body(init_body_size, 'a');
  codec_client_->sendData(*request_encoder_, init_body, false);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);

  timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(20));
  const uint32_t body_size = 2000;
  std::string req_body(body_size, 'b');
  codec_client_->sendData(*request_encoder_, req_body, false);
  codec_client_->sendData(*request_encoder_, "", true);

  bool end_stream = false;
  while (!end_stream) {
    processRequestBodyMessage(*grpc_upstreams_[0], false,
                              [&end_stream](const HttpBody& body, BodyResponse&) {
                                end_stream = body.end_of_stream();
                                return true;
                              });
  }

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  // Upstream should receive 3000 bytes data.
  EXPECT_EQ(upstream_request_->body().length(), body_size + init_body_size);

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  verifyDownstreamResponse(*response, 200);
}

// Test Ext_Proc filter and WebSocket configuration combination.
TEST_P(ExtProcMiscIntegrationTest, WebSocketExtProcCombo) {
  scoped_runtime_.mergeValues({{"envoy.reloadable_features.skip_ext_proc_on_local_reply", "true"}});
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.router_filter_resetall_on_local_reply", "false"}});
  websocketExtProcTest();
}

// TODO(yanjunxiang-google): Delete this test after both runtime flags are removed.
TEST_P(ExtProcMiscIntegrationTest, UpstreamRequestEncoderDanglingPointerTest) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.skip_ext_proc_on_local_reply", "false"}});
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.router_filter_resetall_on_local_reply", "true"}});
  websocketExtProcTest();
}

} // namespace Envoy
