#include <algorithm>

#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/network/address.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/extensions/filters/http/ext_proc/config.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {

using envoy::config::route::v3::Route;
using envoy::config::route::v3::VirtualHost;
using envoy::extensions::filters::http::ext_proc::v3::ExtProcPerRoute;
using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;
using Envoy::Protobuf::MapPair;
using Envoy::ProtobufWkt::Any;
using envoy::service::ext_proc::v3::BodyResponse;
using envoy::service::ext_proc::v3::CommonResponse;
using envoy::service::ext_proc::v3::HeadersResponse;
using envoy::service::ext_proc::v3::HttpBody;
using envoy::service::ext_proc::v3::HttpHeaders;
using envoy::service::ext_proc::v3::HttpTrailers;
using envoy::service::ext_proc::v3::ImmediateResponse;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;
using envoy::service::ext_proc::v3::TrailersResponse;
using Extensions::HttpFilters::ExternalProcessing::HasNoHeader;
using Extensions::HttpFilters::ExternalProcessing::HeaderProtosEqual;
using Extensions::HttpFilters::ExternalProcessing::SingleHeaderValueIs;

using Http::LowerCaseString;

using namespace std::chrono_literals;

// These tests exercise the ext_proc filter through Envoy's integration test
// environment by configuring an instance of the Envoy server and driving it
// through the mock network stack.
class ExtProcIntegrationTest : public HttpIntegrationTest,
                               public Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing {
protected:
  ExtProcIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();

    // Create separate "upstreams" for ExtProc gRPC servers
    for (int i = 0; i < 2; ++i) {
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

  void initializeConfig() {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Ensure "HTTP2 with no prior knowledge." Necessary for gRPC and for headers
      ConfigHelper::setHttp2(
          *(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0)));

      // Clusters for ExtProc gRPC servers, starting by copying an existing cluster
      for (size_t i = 0; i < grpc_upstreams_.size(); ++i) {
        auto* server_cluster = bootstrap.mutable_static_resources()->add_clusters();
        server_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        std::string cluster_name = absl::StrCat("ext_proc_server_", i);
        server_cluster->set_name(cluster_name);
        server_cluster->mutable_load_assignment()->set_cluster_name(cluster_name);
      }

      // Load configuration of the server from YAML and use a helper to add a grpc_service
      // stanza pointing to the cluster that we just made
      setGrpcService(*proto_config_.mutable_grpc_service(), "ext_proc_server_0",
                     grpc_upstreams_[0]->localAddress());

      // Construct a configuration proto for our filter and then re-write it
      // to JSON so that we can add it to the overall config
      envoy::config::listener::v3::Filter ext_proc_filter;
      ext_proc_filter.set_name("envoy.filters.http.ext_proc");
      ext_proc_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrDie(ext_proc_filter));

      // Parameterize with defer processing to prevent bit rot as filter made
      // assumptions of data flow, prior relying on eager processing.
      config_helper_.addRuntimeOverride(Runtime::defer_processing_backedup_streams,
                                        deferredProcessing() ? "true" : "false");
    });
    setUpstreamProtocol(Http::CodecType::HTTP2);
    setDownstreamProtocol(Http::CodecType::HTTP2);
  }

  void setPerRouteConfig(Route* route, const ExtProcPerRoute& cfg) {
    Any cfg_any;
    ASSERT_TRUE(cfg_any.PackFrom(cfg));
    route->mutable_typed_per_filter_config()->insert(
        MapPair<std::string, Any>("envoy.filters.http.ext_proc", cfg_any));
  }

  void setPerHostConfig(VirtualHost& vh, const ExtProcPerRoute& cfg) {
    Any cfg_any;
    ASSERT_TRUE(cfg_any.PackFrom(cfg));
    vh.mutable_typed_per_filter_config()->insert(
        MapPair<std::string, Any>("envoy.filters.http.ext_proc", cfg_any));
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

  IntegrationStreamDecoderPtr sendDownstreamRequestWithBody(
      absl::string_view body,
      absl::optional<std::function<void(Http::RequestHeaderMap& headers)>> modify_headers) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    headers.setMethod("POST");
    if (modify_headers) {
      (*modify_headers)(headers);
    }
    return codec_client_->makeRequestWithBody(headers, std::string(body));
  }

  void verifyDownstreamResponse(IntegrationStreamDecoder& response, int status_code) {
    ASSERT_TRUE(response.waitForEndStream());
    EXPECT_TRUE(response.complete());
    EXPECT_EQ(std::to_string(status_code), response.headers().getStatusValue());
  }

  void handleUpstreamRequest() {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, true);
  }

  void handleUpstreamRequestWithTrailer() {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, false);
    upstream_request_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"x-test-trailers", "Yes"}});
  }

  void handleUpstreamRequestWithResponse(const Buffer::Instance& all_data, uint64_t chunk_size) {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

    // Copy the data so that we don't modify it
    Buffer::OwnedImpl total_response = all_data;
    while (total_response.length() > 0) {
      auto to_move = std::min(total_response.length(), chunk_size);
      Buffer::OwnedImpl chunk;
      chunk.move(total_response, to_move);
      EXPECT_EQ(to_move, chunk.length());
      upstream_request_->encodeData(chunk, false);
    }
    upstream_request_->encodeData(0, true);
  }

  void waitForFirstMessage(FakeUpstream& grpc_upstream, ProcessingRequest& request) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
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
    ProcessingResponse response;
    auto* body = response.mutable_request_body();
    const bool sendReply = !cb || (*cb)(request.request_body(), *body);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void processResponseBodyMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpBody&, BodyResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(request.has_response_body());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* body = response.mutable_response_body();
    const bool sendReply = !cb || (*cb)(request.response_body(), *body);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void processResponseTrailersMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpTrailers&, TrailersResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(request.has_response_trailers());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* body = response.mutable_response_trailers();
    const bool sendReply = !cb || (*cb)(request.response_trailers(), *body);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void processAndRespondImmediately(FakeUpstream& grpc_upstream, bool first_message,
                                    absl::optional<std::function<void(ImmediateResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* immediate = response.mutable_immediate_response();
    if (cb) {
      (*cb)(*immediate);
    }
    processor_stream_->sendGrpcMessage(response);
  }

  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config_{};
  std::vector<FakeUpstream*> grpc_upstreams_;
  FakeHttpConnectionPtr processor_connection_;
  FakeStreamPtr processor_stream_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDeferredProcessing, ExtProcIntegrationTest,
    GRPC_CLIENT_INTEGRATION_DEFERRED_PROCESSING_PARAMS,
    Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing::protocolTestParamsToString);

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by immediately closing the stream.
TEST_P(ExtProcIntegrationTest, GetAndCloseStream) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  // Just close the stream without doing anything
  processor_stream_->startGrpcStream();
  processor_stream_->finishGrpcStream(Grpc::Status::Ok);

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by returning a failure before the first stream response can be sent.
TEST_P(ExtProcIntegrationTest, GetAndFailStream) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  // Fail the stream immediately
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);
  verifyDownstreamResponse(*response, 500);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully, but then sends a gRPC error.
TEST_P(ExtProcIntegrationTest, GetAndFailStreamOutOfLine) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  // Fail the stream in between messages
  processor_stream_->finishGrpcStream(Grpc::Status::Internal);

  verifyDownstreamResponse(*response, 500);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully, but then sends a gRPC error.
TEST_P(ExtProcIntegrationTest, GetAndFailStreamOutOfLineLater) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  // Fail the stream in between messages
  processor_stream_->finishGrpcStream(Grpc::Status::Internal);

  verifyDownstreamResponse(*response, 500);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully but closes the stream after response_headers.
TEST_P(ExtProcIntegrationTest, GetAndCloseStreamOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  handleUpstreamRequest();

  ProcessingRequest response_headers_msg;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, response_headers_msg));
  processor_stream_->finishGrpcStream(Grpc::Status::Ok);

  verifyDownstreamResponse(*response, 200);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully but then fails on the response_headers message.
TEST_P(ExtProcIntegrationTest, GetAndFailStreamOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  handleUpstreamRequest();

  ProcessingRequest response_headers_msg;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, response_headers_msg));
  processor_stream_->finishGrpcStream(Grpc::Status::Internal);

  verifyDownstreamResponse(*response, 500);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by requesting to modify the request headers.
TEST_P(ExtProcIntegrationTest, GetAndSetHeaders) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("x-remove-this"), "yes"); });

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders& headers, HeadersResponse& headers_resp) {
        Http::TestRequestHeaderMapImpl expected_request_headers{
            {":scheme", "http"}, {":method", "GET"},       {"host", "host"},
            {":path", "/"},      {"x-remove-this", "yes"}, {"x-forwarded-proto", "http"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));

        auto response_header_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* mut1 = response_header_mutation->add_set_headers();
        mut1->mutable_header()->set_key("x-new-header");
        mut1->mutable_header()->set_value("new");
        response_header_mutation->add_remove_headers("x-remove-this");
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_THAT(upstream_request_->headers(), HasNoHeader("x-remove-this"));
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("x-new-header", "new"));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders& headers, HeadersResponse&) {
        Http::TestRequestHeaderMapImpl expected_response_headers{{":status", "200"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_response_headers));
        return true;
      });

  verifyDownstreamResponse(*response, 200);
}

// Test the filter with body buffering turned on, but sending a GET
// and a response that both have no body.
TEST_P(ExtProcIntegrationTest, GetBufferedButNoBodies) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_TRUE(headers.end_of_stream());
                                 return true;
                               });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{
          {":status", "200"},
          {"content-length", "0"},
      },
      true);

  processResponseHeadersMessage(*grpc_upstreams_[0], false,
                                [](const HttpHeaders& headers, HeadersResponse&) {
                                  EXPECT_TRUE(headers.end_of_stream());
                                  return true;
                                });

  verifyDownstreamResponse(*response, 200);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the response_headers message
// by requesting to modify the response headers.
TEST_P(ExtProcIntegrationTest, GetAndSetHeadersOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* response_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* add1 = response_mutation->add_set_headers();
        add1->mutable_header()->set_key("x-response-processed");
        add1->mutable_header()->set_value("1");
        auto* add2 = response_mutation->add_set_headers();
        add2->mutable_header()->set_key(":status");
        add2->mutable_header()->set_value("201");
        return true;
      });

  verifyDownstreamResponse(*response, 201);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-response-processed", "1"));
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the response_headers message
// but tries to set the status code to an invalid value
TEST_P(ExtProcIntegrationTest, GetAndSetHeadersOnResponseBadStatus) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* response_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* add1 = response_mutation->add_set_headers();
        add1->mutable_header()->set_key("x-response-processed");
        add1->mutable_header()->set_value("1");
        auto* add2 = response_mutation->add_set_headers();
        add2->mutable_header()->set_key(":status");
        add2->mutable_header()->set_value("100");
        return true;
      });

  // Invalid status code should be ignored, but the other header mutation
  // should still have been processed.
  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-response-processed", "1"));
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the response_headers message
// but tries to set the status code to two values. The second
// attempt should be ignored.
TEST_P(ExtProcIntegrationTest, GetAndSetHeadersOnResponseTwoStatuses) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* response_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* add1 = response_mutation->add_set_headers();
        add1->mutable_header()->set_key("x-response-processed");
        add1->mutable_header()->set_value("1");
        auto* add2 = response_mutation->add_set_headers();
        add2->mutable_header()->set_key(":status");
        add2->mutable_header()->set_value("201");
        auto* add3 = response_mutation->add_set_headers();
        add3->mutable_header()->set_key(":status");
        add3->mutable_header()->set_value("202");
        add3->mutable_append()->set_value(true);
        return true;
      });

  // Invalid status code should be ignored, but the other header mutation
  // should still have been processed.
  verifyDownstreamResponse(*response, 201);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-response-processed", "1"));
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the response_headers message
// by checking the headers and modifying the trailers
TEST_P(ExtProcIntegrationTest, GetAndSetHeadersAndTrailersOnResponse) {
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequestWithTrailer();

  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  processResponseTrailersMessage(
      *grpc_upstreams_[0], false, [](const HttpTrailers& trailers, TrailersResponse& resp) {
        Http::TestResponseTrailerMapImpl expected_trailers{{"x-test-trailers", "Yes"}};
        EXPECT_THAT(trailers.trailers(), HeaderProtosEqual(expected_trailers));
        auto* trailer_mut = resp.mutable_header_mutation();
        auto* trailer_add = trailer_mut->add_set_headers();
        trailer_add->mutable_header()->set_key("x-modified-trailers");
        trailer_add->mutable_header()->set_value("xxx");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  ASSERT_TRUE(response->trailers());
  EXPECT_THAT(*(response->trailers()), SingleHeaderValueIs("x-test-trailers", "Yes"));
  EXPECT_THAT(*(response->trailers()), SingleHeaderValueIs("x-modified-trailers", "xxx"));
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that tries to modify the trailers incorrectly
// according to the header mutation rules.
TEST_P(ExtProcIntegrationTest, GetAndSetTrailersIncorrectlyOnResponse) {
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);
  proto_config_.mutable_mutation_rules()->mutable_disallow_all()->set_value(true);
  proto_config_.mutable_mutation_rules()->mutable_disallow_is_error()->set_value(true);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequestWithTrailer();

  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  processResponseTrailersMessage(
      *grpc_upstreams_[0], false, [](const HttpTrailers&, TrailersResponse& resp) {
        auto* trailer_add = resp.mutable_header_mutation()->add_set_headers();
        trailer_add->mutable_header()->set_key("x-modified-trailers");
        trailer_add->mutable_header()->set_value("xxx");
        return true;
      });

  if (Runtime::runtimeFeatureEnabled(Runtime::defer_processing_backedup_streams)) {
    // We get a reset since we've received some of the response already.
    ASSERT_TRUE(response->waitForReset());
  } else {
    verifyDownstreamResponse(*response, 500);
  }
}

// Test the filter configured to only send the response trailers message
TEST_P(ExtProcIntegrationTest, GetAndSetOnlyTrailersOnResponse) {
  auto* mode = proto_config_.mutable_processing_mode();
  mode->set_request_header_mode(ProcessingMode::SKIP);
  mode->set_response_header_mode(ProcessingMode::SKIP);
  mode->set_response_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  handleUpstreamRequestWithTrailer();
  processResponseTrailersMessage(
      *grpc_upstreams_[0], true, [](const HttpTrailers& trailers, TrailersResponse& resp) {
        Http::TestResponseTrailerMapImpl expected_trailers{{"x-test-trailers", "Yes"}};
        EXPECT_THAT(trailers.trailers(), HeaderProtosEqual(expected_trailers));
        auto* trailer_mut = resp.mutable_header_mutation();
        auto* trailer_add = trailer_mut->add_set_headers();
        trailer_add->mutable_header()->set_key("x-modified-trailers");
        trailer_add->mutable_header()->set_value("xxx");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  ASSERT_TRUE(response->trailers());
  EXPECT_THAT(*(response->trailers()), SingleHeaderValueIs("x-test-trailers", "Yes"));
  EXPECT_THAT(*(response->trailers()), SingleHeaderValueIs("x-modified-trailers", "xxx"));
}

// Test the filter with a response body callback enabled using an
// an ext_proc server that responds to the response_body message
// by requesting to modify the response body and headers.
TEST_P(ExtProcIntegrationTest, GetAndSetBodyAndHeadersOnResponse) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* content_length =
            headers_resp.mutable_response()->mutable_header_mutation()->add_set_headers();
        content_length->mutable_header()->set_key("content-length");
        content_length->mutable_header()->set_value("13");
        return true;
      });

  // Should get just one message with the body
  processResponseBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body("Hello, World!");
        auto* header_mut = body_resp.mutable_response()->mutable_header_mutation();
        auto* header_add = header_mut->add_set_headers();
        header_add->mutable_header()->set_key("x-testing-response-header");
        header_add->mutable_header()->set_value("Yes");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-testing-response-header", "Yes"));
  EXPECT_EQ("Hello, World!", response->body());
}

// Test the filter with a response body callback enabled that uses
// partial buffering. We should still be able to change headers.
TEST_P(ExtProcIntegrationTest, GetAndSetBodyAndHeadersOnResponsePartialBuffered) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED_PARTIAL);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);

  // Should get just one message with the body
  processResponseBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody&, BodyResponse& body_resp) {
        auto* header_mut = body_resp.mutable_response()->mutable_header_mutation();
        auto* header_add = header_mut->add_set_headers();
        header_add->mutable_header()->set_key("x-testing-response-header");
        header_add->mutable_header()->set_value("Yes");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-testing-response-header", "Yes"));
}

// Test the filter with a response body callback enabled using an
// an ext_proc server that responds to the response_body message
// by requesting to modify the response body and headers.
TEST_P(ExtProcIntegrationTest, GetAndSetBodyAndHeadersAndTrailersOnResponse) {
  auto* mode = proto_config_.mutable_processing_mode();
  mode->set_response_body_mode(ProcessingMode::BUFFERED);
  mode->set_response_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequestWithTrailer();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);

  // Should get just one message with the body
  processResponseBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    return true;
  });

  processResponseTrailersMessage(
      *grpc_upstreams_[0], false, [](const HttpTrailers& trailers, TrailersResponse& resp) {
        Http::TestResponseTrailerMapImpl expected_trailers{{"x-test-trailers", "Yes"}};
        EXPECT_THAT(trailers.trailers(), HeaderProtosEqual(expected_trailers));
        auto* trailer_mut = resp.mutable_header_mutation();
        auto* trailer_add = trailer_mut->add_set_headers();
        trailer_add->mutable_header()->set_key("x-modified-trailers");
        trailer_add->mutable_header()->set_value("xxx");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  ASSERT_TRUE(response->trailers());
  EXPECT_THAT(*(response->trailers()), SingleHeaderValueIs("x-test-trailers", "Yes"));
  EXPECT_THAT(*(response->trailers()), SingleHeaderValueIs("x-modified-trailers", "xxx"));
}

// Test the filter using a configuration that processes response trailers, and process an upstream
// response that has no trailers, so add them.
TEST_P(ExtProcIntegrationTest, AddTrailersOnResponse) {
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  processResponseTrailersMessage(*grpc_upstreams_[0], false,
                                 [](const HttpTrailers&, TrailersResponse& resp) {
                                   auto* trailer_mut = resp.mutable_header_mutation();
                                   auto* trailer_add = trailer_mut->add_set_headers();
                                   trailer_add->mutable_header()->set_key("x-modified-trailers");
                                   trailer_add->mutable_header()->set_value("xxx");
                                   return true;
                                 });

  verifyDownstreamResponse(*response, 200);
  ASSERT_TRUE(response->trailers());
  EXPECT_THAT(*(response->trailers()), SingleHeaderValueIs("x-modified-trailers", "xxx"));
}

// Test the filter using a configuration that processes response trailers, and process an upstream
// response that has no trailers, but when the time comes, don't actually add anything.
TEST_P(ExtProcIntegrationTest, AddTrailersOnResponseJustKidding) {
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  processResponseTrailersMessage(*grpc_upstreams_[0], false, absl::nullopt);

  verifyDownstreamResponse(*response, 200);
}

// Test the filter with a response body callback enabled using an
// an ext_proc server that responds to the response_body message
// by requesting to modify the response body and headers, using a response
// big enough to require multiple chunks.
TEST_P(ExtProcIntegrationTest, GetAndSetBodyAndHeadersOnBigResponse) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);

  Buffer::OwnedImpl full_response;
  TestUtility::feedBufferWithRandomCharacters(full_response, 4000);
  handleUpstreamRequestWithResponse(full_response, 1000);

  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  // Should get just one message with the body
  processResponseBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* header_mut = body_resp.mutable_response()->mutable_header_mutation();
        auto* header_add = header_mut->add_set_headers();
        header_add->mutable_header()->set_key("x-testing-response-header");
        header_add->mutable_header()->set_value("Yes");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-testing-response-header", "Yes"));
}

// Test the filter with both body callbacks enabled and have the
// ext_proc server change both of them.
TEST_P(ExtProcIntegrationTest, GetAndSetBodyOnBoth) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* content_length =
            headers_resp.mutable_response()->mutable_header_mutation()->add_set_headers();
        content_length->mutable_header()->set_key("content-length");
        content_length->mutable_header()->set_value("13");
        return true;
      });

  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body("Hello, World!");
        return true;
      });

  handleUpstreamRequest();

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        headers_resp.mutable_response()->mutable_header_mutation()->add_remove_headers(
            "content-length");
        return true;
      });

  processResponseBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        body_resp.mutable_response()->mutable_body_mutation()->set_body("123");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  EXPECT_EQ("123", response->body());
}

// Test the filter using a configuration that uses the processing mode to
// only send the response_headers message.
TEST_P(ExtProcIntegrationTest, ProcessingModeResponseOnly) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  handleUpstreamRequest();

  processResponseHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* response_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* add1 = response_mutation->add_set_headers();
        add1->mutable_header()->set_key("x-response-processed");
        add1->mutable_header()->set_value("1");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-response-processed", "1"));
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by sending back an immediate_response message, which should be
// returned directly to the downstream.
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediately) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processAndRespondImmediately(*grpc_upstreams_[0], true, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
    auto* hdr1 = immediate.mutable_headers()->add_set_headers();
    hdr1->mutable_header()->set_key("x-failure-reason");
    hdr1->mutable_header()->set_value("testing");
    auto* hdr2 = immediate.mutable_headers()->add_set_headers();
    hdr2->mutable_header()->set_key("content-type");
    hdr2->mutable_header()->set_value("application/json");
  });

  verifyDownstreamResponse(*response, 401);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-failure-reason", "testing"));
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("content-type", "application/json"));
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by sending back an immediate_response message after the
// request_headers message
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();

  processAndRespondImmediately(*grpc_upstreams_[0], false, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
  });

  verifyDownstreamResponse(*response, 401);
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}

// Test the filter with request body buffering enabled using
// an ext_proc server that responds to the request_body message
// by sending back an immediate_response message
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyOnRequestBody) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processAndRespondImmediately(*grpc_upstreams_[0], false, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
  });

  verifyDownstreamResponse(*response, 401);
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}

// Test the filter with body buffering enabled using
// an ext_proc server that responds to the response_body message
// by sending back an immediate_response message. Since this
// happens after the response headers have been sent, as a result
// Envoy should just reset the stream.
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyOnResponseBody) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);

  processAndRespondImmediately(*grpc_upstreams_[0], false, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
  });

  // Since we are stopping iteration on headers, and since the response is short,
  // we actually get an error message here.
  verifyDownstreamResponse(*response, 401);
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}

// Test the filter using an ext_proc server that responds to the request_body message
// by sending back an immediate_response message with an invalid status code.
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyWithBadStatus) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);
  processAndRespondImmediately(*grpc_upstreams_[0], true, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Continue);
    immediate.set_body("{\"reason\": \"Because\"}");
    immediate.set_details("Failed because we said so");
  });

  // The attempt to set the status code to 100 should have been ignored.
  verifyDownstreamResponse(*response, 200);
  EXPECT_EQ("{\"reason\": \"Because\"}", response->body());
}

// Test the filter with request body buffering enabled using
// an ext_proc server that responds to the request_body message
// by modifying a header that should cause an error.
TEST_P(ExtProcIntegrationTest, GetAndIncorrectlyModifyHeaderOnBody) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_mutation_rules()->mutable_disallow_is_error()->set_value(true);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("Original body", absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody&, BodyResponse& response) {
        auto* mut = response.mutable_response()->mutable_header_mutation()->add_set_headers();
        mut->mutable_header()->set_key(":scheme");
        mut->mutable_header()->set_value("tcp");
        return true;
      });

  verifyDownstreamResponse(*response, 500);
}

// Test the filter with request body buffering enabled using
// an ext_proc server that responds to the request_body message
// by modifying a header that should cause an error.
TEST_P(ExtProcIntegrationTest, GetAndIncorrectlyModifyHeaderOnBodyPartialBuffer) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED_PARTIAL);
  proto_config_.mutable_mutation_rules()->mutable_disallow_is_error()->set_value(true);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("Original body", absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody&, BodyResponse& response) {
        auto* mut = response.mutable_response()->mutable_header_mutation()->add_set_headers();
        mut->mutable_header()->set_key(":scheme");
        mut->mutable_header()->set_value("tcp");
        return true;
      });

  verifyDownstreamResponse(*response, 500);
}

// Test the ability of the filter to turn a GET into a POST by adding a body
// and changing the method.
TEST_P(ExtProcIntegrationTest, ConvertGetToPost) {
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* header_mut = headers_resp.mutable_response()->mutable_header_mutation();
        auto* method = header_mut->add_set_headers();
        method->mutable_header()->set_key(":method");
        method->mutable_header()->set_value("POST");
        auto* content_type = header_mut->add_set_headers();
        content_type->mutable_header()->set_key("content-type");
        content_type->mutable_header()->set_value("text/plain");
        headers_resp.mutable_response()->mutable_body_mutation()->set_body("Hello, Server!");
        // This special status tells us to replace the whole request
        headers_resp.mutable_response()->set_status(CommonResponse::CONTINUE_AND_REPLACE);
        return true;
      });

  handleUpstreamRequest();

  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs(":method", "POST"));
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("content-type", "text/plain"));
  EXPECT_EQ(upstream_request_->bodyLength(), 14);
  EXPECT_EQ(upstream_request_->body().toString(), "Hello, Server!");

  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Test the ability of the filter to completely replace a request message with a new
// request message.
TEST_P(ExtProcIntegrationTest, ReplaceCompleteRequest) {
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        headers_resp.mutable_response()->mutable_body_mutation()->set_body("Hello, Server!");
        // This special status tells us to replace the whole request
        headers_resp.mutable_response()->set_status(CommonResponse::CONTINUE_AND_REPLACE);
        return true;
      });

  handleUpstreamRequest();

  // Ensure that we replaced and did not append to the request.
  EXPECT_EQ(upstream_request_->body().toString(), "Hello, Server!");

  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Test the ability of the filter to completely replace a request message with a new
// request message.
TEST_P(ExtProcIntegrationTest, ReplaceCompleteRequestBuffered) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        headers_resp.mutable_response()->mutable_body_mutation()->set_body("Hello, Server!");
        // This special status tells us to replace the whole request
        headers_resp.mutable_response()->set_status(CommonResponse::CONTINUE_AND_REPLACE);
        return true;
      });

  // Even though we set the body mode to BUFFERED, we should receive no callback because
  // we returned CONTINUE_AND_REPLACE.

  handleUpstreamRequest();

  // Ensure that we replaced and did not append to the request.
  EXPECT_EQ(upstream_request_->body().toString(), "Hello, Server!");

  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Send a request, but wait longer than the "message timeout" before sending a response
// from the external processor. This should trigger the timeout and result
// in a 500 error.
TEST_P(ExtProcIntegrationTest, RequestMessageTimeout) {
  // ensure 200 ms timeout
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [this](const HttpHeaders&, HeadersResponse&) {
                                 // Travel forward 400 ms
                                 timeSystem().advanceTimeWaitImpl(400ms);
                                 return false;
                               });

  // We should immediately have an error response now
  verifyDownstreamResponse(*response, 500);
}

// Same as the previous test but on the response path, since there are separate
// timers for each.
TEST_P(ExtProcIntegrationTest, ResponseMessageTimeout) {
  // ensure 200 ms timeout
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false,
                                [this](const HttpHeaders&, HeadersResponse&) {
                                  // Travel forward 400 ms
                                  timeSystem().advanceTimeWaitImpl(400ms);
                                  return false;
                                });

  // We should immediately have an error response now
  verifyDownstreamResponse(*response, 500);
}

// Send a request,  wait longer than the "message timeout" before sending a response
// from the external processor, but nothing should happen because we are ignoring
// the timeout.
TEST_P(ExtProcIntegrationTest, RequestMessageTimeoutIgnoreError) {
  proto_config_.set_failure_mode_allow(true);
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [this](const HttpHeaders&, HeadersResponse&) {
                                 // Travel forward 400 ms
                                 timeSystem().advanceTimeWaitImpl(400ms);
                                 return false;
                               });

  // We should be able to continue from here since the error was ignored
  handleUpstreamRequest();
  // Since we are ignoring errors, the late response to the request headers
  // message meant that subsequent messages are spurious and the response
  // headers message was never sent.
  // Despite the timeout the request should succeed.
  verifyDownstreamResponse(*response, 200);
}

// Same as the previous test but on the response path, since there are separate
// timers for each.
TEST_P(ExtProcIntegrationTest, ResponseMessageTimeoutIgnoreError) {
  proto_config_.set_failure_mode_allow(true);
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false,
                                [this](const HttpHeaders&, HeadersResponse&) {
                                  // Travel forward 400 ms
                                  timeSystem().advanceTimeWaitImpl(400ms);
                                  return false;
                                });

  // We should still succeed despite the timeout
  verifyDownstreamResponse(*response, 200);
}

// While waiting for a response from the external processor, trigger a
// downstream disconnect followed by a response message timeout.
TEST_P(ExtProcIntegrationTest, ResponseMessageTimeoutDownstreamDisconnect) {
  proto_config_.set_failure_mode_allow(true);
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false,
                                [this](const HttpHeaders&, HeadersResponse&) {
                                  // Downstream disconnect
                                  codec_client_->close();
                                  // Travel forward 400 ms
                                  timeSystem().advanceTimeWaitImpl(400ms);
                                  return false;
                                });

  ASSERT_TRUE(response->waitForReset());
}

// Test how the filter responds when asked to buffer a request body for a POST
// request with an empty body. We should get an empty body message because
// the Envoy filter stream received the body after all the headers.
TEST_P(ExtProcIntegrationTest, BufferBodyOverridePostWithEmptyBody) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("", absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_FALSE(headers.end_of_stream());
                                 return true;
                               });
  // We should get an empty body message this time
  processRequestBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    EXPECT_EQ(body.body().size(), 0);
    return true;
  });

  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to buffer a response body for a POST
// request with an empty body. We should get an empty body message because
// the Envoy filter stream received the body after all the headers.
TEST_P(ExtProcIntegrationTest, BufferBodyOverrideGetWithEmptyResponseBody) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_TRUE(headers.end_of_stream());
                                 return true;
                               });
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(0, true);
  processResponseHeadersMessage(*grpc_upstreams_[0], false,
                                [](const HttpHeaders& headers, HeadersResponse&) {
                                  EXPECT_FALSE(headers.end_of_stream());
                                  return true;
                                });
  // We should get an empty body message this time
  processResponseBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    EXPECT_EQ(body.body().size(), 0);
    return true;
  });
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to buffer a response body for a POST
// request with no body. We should not get an empty body message because
// the Envoy filter stream received headers with no body.
TEST_P(ExtProcIntegrationTest, BufferBodyOverrideGetWithNoResponseBody) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_TRUE(headers.end_of_stream());
                                 return true;
                               });
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  processResponseHeadersMessage(*grpc_upstreams_[0], false,
                                [](const HttpHeaders& headers, HeadersResponse&) {
                                  EXPECT_TRUE(headers.end_of_stream());
                                  return true;
                                });
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to stream a request body for a POST
// request with an empty body. We should get an empty body message because
// the Envoy filter stream received the body after all the headers.
TEST_P(ExtProcIntegrationTest, BufferBodyOverridePostWithEmptyBodyStreamed) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("", absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_FALSE(headers.end_of_stream());
                                 return true;
                               });
  // We should get an empty body message this time
  processRequestBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    EXPECT_EQ(body.body().size(), 0);
    return true;
  });

  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to stream a request body for a POST
// request with an empty body in "buffered partial" mode. We should get an empty body message
// because the Envoy filter stream received the body after all the headers.
TEST_P(ExtProcIntegrationTest, BufferBodyOverridePostWithEmptyBodyBufferedPartial) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED_PARTIAL);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("", absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_FALSE(headers.end_of_stream());
                                 return true;
                               });
  // We should get an empty body message this time
  processRequestBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    EXPECT_EQ(body.body().size(), 0);
    return true;
  });

  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to buffer a request body for a GET
// request with no body. We should receive no body message because the Envoy
// filter stream received the headers and end simultaneously.
TEST_P(ExtProcIntegrationTest, BufferBodyOverrideGetRequestNoBody) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_TRUE(headers.end_of_stream());
                                 return true;
                               });
  // We should not see a request body message here
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to stream a request body for a GET
// request with no body. We should receive no body message because the Envoy
// filter stream received the headers and end simultaneously.
TEST_P(ExtProcIntegrationTest, BufferBodyOverrideGetRequestNoBodyStreaming) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_TRUE(headers.end_of_stream());
                                 return true;
                               });
  // We should not see a request body message here
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to stream a request body for a GET
// request with no body in "buffered partial" mode. We should receive no body message because the
// Envoy filter stream received the headers and end simultaneously.
TEST_P(ExtProcIntegrationTest, BufferBodyOverrideGetRequestNoBodyBufferedPartial) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED_PARTIAL);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_TRUE(headers.end_of_stream());
                                 return true;
                               });
  // We should not see a request body message here
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to buffer a request body for a POST
// request with a body.
TEST_P(ExtProcIntegrationTest, BufferBodyOverridePostWithRequestBody) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("Testing", absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_FALSE(headers.end_of_stream());
                                 return true;
                               });
  processRequestBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    EXPECT_EQ(body.body(), "Testing");
    return true;
  });
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Set up per-route configuration that sets a custom processing mode on the
// route, and test that the processing mode takes effect.
TEST_P(ExtProcIntegrationTest, PerRouteProcessingMode) {
  initializeConfig();
  config_helper_.addConfigModifier([this](HttpConnectionManager& cm) {
    // Set up "/foo" so that it will send a buffered body
    auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
    auto* route = vh->mutable_routes()->Mutable(0);
    route->mutable_match()->set_path("/foo");
    ExtProcPerRoute per_route;
    per_route.mutable_overrides()->mutable_processing_mode()->set_response_body_mode(
        ProcessingMode::BUFFERED);
    setPerRouteConfig(route, per_route);
  });
  HttpIntegrationTest::initialize();

  auto response =
      sendDownstreamRequest([](Http::RequestHeaderMap& headers) { headers.setPath("/foo"); });
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  Buffer::OwnedImpl full_response;
  TestUtility::feedBufferWithRandomCharacters(full_response, 100);
  handleUpstreamRequestWithResponse(full_response, 100);
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  // Because of the per-route config we should get a buffered response
  processResponseBodyMessage(*grpc_upstreams_[0], false,
                             [&full_response](const HttpBody& body, BodyResponse&) {
                               EXPECT_TRUE(body.end_of_stream());
                               EXPECT_EQ(body.body(), full_response.toString());
                               return true;
                             });
  verifyDownstreamResponse(*response, 200);
}

// Set up configuration on the virtual host and on the route and see that
// the two are merged.
TEST_P(ExtProcIntegrationTest, PerRouteAndHostProcessingMode) {
  initializeConfig();
  config_helper_.addConfigModifier([this](HttpConnectionManager& cm) {
    auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
    // Set up a processing mode for the host that should not be honored
    ExtProcPerRoute per_host;
    per_host.mutable_overrides()->mutable_processing_mode()->set_request_header_mode(
        ProcessingMode::SKIP);
    per_host.mutable_overrides()->mutable_processing_mode()->set_response_header_mode(
        ProcessingMode::SKIP);
    setPerHostConfig(*vh, per_host);

    // Set up "/foo" so that it will send a buffered body
    auto* route = vh->mutable_routes()->Mutable(0);
    route->mutable_match()->set_path("/foo");
    ExtProcPerRoute per_route;
    per_route.mutable_overrides()->mutable_processing_mode()->set_response_body_mode(
        ProcessingMode::BUFFERED);
    setPerRouteConfig(route, per_route);
  });
  HttpIntegrationTest::initialize();

  auto response =
      sendDownstreamRequest([](Http::RequestHeaderMap& headers) { headers.setPath("/foo"); });
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  Buffer::OwnedImpl full_response;
  TestUtility::feedBufferWithRandomCharacters(full_response, 100);
  handleUpstreamRequestWithResponse(full_response, 100);
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  // Because of the per-route config we should get a buffered response.
  // If the config from the host is applied then this won't work.
  processResponseBodyMessage(*grpc_upstreams_[0], false,
                             [&full_response](const HttpBody& body, BodyResponse&) {
                               EXPECT_TRUE(body.end_of_stream());
                               EXPECT_EQ(body.body(), full_response.toString());
                               return true;
                             });
  verifyDownstreamResponse(*response, 200);
}

// Set up per-route configuration that disables ext_proc for a route and ensure
// that it is not called.
TEST_P(ExtProcIntegrationTest, PerRouteDisable) {
  initializeConfig();
  config_helper_.addConfigModifier([this](HttpConnectionManager& cm) {
    // Set up "/foo" so that ext_proc is disabled
    auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
    auto* route = vh->mutable_routes()->Mutable(0);
    route->mutable_match()->set_path("/foo");
    ExtProcPerRoute per_route;
    per_route.set_disabled(true);
    setPerRouteConfig(route, per_route);
  });
  HttpIntegrationTest::initialize();

  auto response =
      sendDownstreamRequest([](Http::RequestHeaderMap& headers) { headers.setPath("/foo"); });
  // There should be no ext_proc processing here
  Buffer::OwnedImpl full_response;
  TestUtility::feedBufferWithRandomCharacters(full_response, 100);
  handleUpstreamRequestWithResponse(full_response, 100);
  verifyDownstreamResponse(*response, 200);
}

// Set up per-route configuration that sets a different GrpcService on the
// route, and verify that it is used instead of the primary GrpcService.
TEST_P(ExtProcIntegrationTest, PerRouteGrpcService) {
  initializeConfig();
  config_helper_.addConfigModifier([this](HttpConnectionManager& cm) {
    // Set up "/foo" so that it will use a different GrpcService
    auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
    auto* route = vh->mutable_routes()->Mutable(0);
    route->mutable_match()->set_path("/foo");
    ExtProcPerRoute per_route;
    setGrpcService(*per_route.mutable_overrides()->mutable_grpc_service(), "ext_proc_server_1",
                   grpc_upstreams_[1]->localAddress());
    setPerRouteConfig(route, per_route);
  });
  HttpIntegrationTest::initialize();

  // Request that matches route directed to ext_proc_server_1
  auto response =
      sendDownstreamRequest([](Http::RequestHeaderMap& headers) { headers.setPath("/foo"); });
  processRequestHeadersMessage(*grpc_upstreams_[1], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(
      *grpc_upstreams_[1], false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* response_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* add1 = response_mutation->add_set_headers();
        add1->mutable_header()->set_key("x-response-processed");
        add1->mutable_header()->set_value("1");
        auto* add2 = response_mutation->add_set_headers();
        add2->mutable_header()->set_key(":status");
        add2->mutable_header()->set_value("201");
        return true;
      });
  verifyDownstreamResponse(*response, 201);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-response-processed", "1"));
}

} // namespace Envoy
