#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "test/common/http/common.h"
#include "test/integration/http_integration.h"

// Tests for status_on_error functionality.
namespace Envoy {

using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;

class ExtProcStatusOnErrorIntegrationTest : public HttpIntegrationTest,
                                            public Grpc::GrpcClientIntegrationParamTest {
protected:
  ExtProcStatusOnErrorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();

    // Create separate "upstreams" for ExtProc gRPC servers
    for (int i = 0; i < grpc_upstream_count_; ++i) {
      grpc_upstreams_.push_back(&addFakeUpstream(Http::CodecType::HTTP2));
    }
  }

  void TearDown() override {
    if (processor_connection_) {
      ASSERT_TRUE(processor_connection_->close());
      ASSERT_TRUE(processor_connection_->waitForDisconnect());
    }
    cleanupUpstreamAndDownstream();
  }

  void initializeConfig(uint32_t status_code) {
    config_helper_.addConfigModifier([this, status_code](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* server_cluster = bootstrap.mutable_static_resources()->add_clusters();
      server_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      server_cluster->set_name("ext_proc_server");
      server_cluster->mutable_load_assignment()->set_cluster_name("ext_proc_server");

      setGrpcService(*proto_config_.mutable_grpc_service(), "ext_proc_server",
                     grpc_upstreams_[0]->localAddress());

      proto_config_.mutable_status_on_error()->set_code(
          static_cast<envoy::type::v3::StatusCode>(status_code));
      proto_config_.set_failure_mode_allow(false);

      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter ext_proc_filter;
      ext_proc_filter.set_name("envoy.filters.http.ext_proc");
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

  void waitForFirstMessage(FakeUpstream& grpc_upstream, ProcessingRequest& request) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  }

  bool IsEnvoyGrpc() { return std::get<1>(GetParam()) == Envoy::Grpc::ClientType::EnvoyGrpc; }

  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config_{};
  FakeStreamPtr processor_stream_;
  FakeHttpConnectionPtr processor_connection_;
  std::vector<FakeUpstream*> grpc_upstreams_;
  int grpc_upstream_count_ = 1;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, ExtProcStatusOnErrorIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);

// Test that status_on_error is used when gRPC stream encounters an error.
TEST_P(ExtProcStatusOnErrorIntegrationTest, GrpcStreamErrorCustomStatus) {
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::EnvoyGrpc);

  initializeConfig(503); // Use 503 Service Unavailable.
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();

  // Send successful response first, then simulate gRPC stream error.
  ProcessingResponse resp;
  resp.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp);

  // Now trigger gRPC stream error which should use status_on_error.
  processor_stream_->finishGrpcStream(Grpc::Status::Internal);

  // Should get custom status code 503 instead of default 500.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

// Test that default status (500) is used when status_on_error is not configured.
TEST_P(ExtProcStatusOnErrorIntegrationTest, GrpcStreamErrorDefaultStatus) {
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::EnvoyGrpc);

  // Initialize without setting status_on_error, should default to 500.
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* server_cluster = bootstrap.mutable_static_resources()->add_clusters();
    server_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    server_cluster->set_name("ext_proc_server");
    server_cluster->mutable_load_assignment()->set_cluster_name("ext_proc_server");

    setGrpcService(*proto_config_.mutable_grpc_service(), "ext_proc_server",
                   grpc_upstreams_[0]->localAddress());

    proto_config_.set_failure_mode_allow(false);

    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter ext_proc_filter;
    ext_proc_filter.set_name("envoy.filters.http.ext_proc");
    ext_proc_filter.mutable_typed_config()->PackFrom(proto_config_);
    config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_proc_filter));
  });

  setUpstreamProtocol(Http::CodecType::HTTP1);
  setDownstreamProtocol(Http::CodecType::HTTP1);

  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();

  // Send successful response first, then simulate gRPC stream error.
  ProcessingResponse resp;
  resp.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp);

  // Trigger gRPC stream error without status_on_error configured.
  processor_stream_->finishGrpcStream(Grpc::Status::Internal);

  // Should get default status code 500.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("500", response->headers().getStatusValue());
}

// Test that message timeout returns 504 Gateway Timeout regardless of status_on_error.
TEST_P(ExtProcStatusOnErrorIntegrationTest, MessageTimeoutReturnsGatewayTimeout) {
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::EnvoyGrpc);

  initializeConfig(502); // Configure 502, but timeout should return 504.
  proto_config_.mutable_message_timeout()->set_nanos(100000000); // 100ms timeout.

  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();

  // Don't send a response to trigger timeout - just wait for the timeout to occur.

  // Let timeout occur.
  test_server_->waitForCounterGe("http.config_test.ext_proc.message_timeouts", 1);

  // Should return 504 Gateway Timeout instead of configured status_on_error.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("504", response->headers().getStatusValue());
}

// Test that status_on_error is used when processing/mutation errors occur.
TEST_P(ExtProcStatusOnErrorIntegrationTest, ProcessingErrorCustomStatus) {
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::EnvoyGrpc);

  initializeConfig(422); // Use 422 Unprocessable Entity.
  // Enable strict mutation rules to trigger processing errors.
  proto_config_.mutable_mutation_rules()->mutable_disallow_is_error()->set_value(true);
  proto_config_.mutable_mutation_rules()->mutable_disallow_system()->set_value(true);

  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  // Process the request and send back invalid system header mutation.
  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();

  ProcessingResponse resp;
  auto* header_mut = resp.mutable_request_headers()->mutable_response()->mutable_header_mutation();
  auto* header = header_mut->add_set_headers();
  header->mutable_append()->set_value(false);
  header->mutable_header()->set_key(":system-header"); // This should trigger error.
  header->mutable_header()->set_raw_value("invalid");
  processor_stream_->sendGrpcMessage(resp);

  // Should get custom status code 422 for processing error.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("422", response->headers().getStatusValue());
}

} // namespace Envoy
