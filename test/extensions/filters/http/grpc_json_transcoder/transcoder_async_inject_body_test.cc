#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_protocol_integration.h"
#include "test/proto/bookstore.pb.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {
namespace {

using Envoy::Protobuf::TextFormat;

constexpr absl::string_view kTranscoderConfig =
    R"(
          name: grpc_json_transcoder
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder
            proto_descriptor : "%s"
            services : "bookstore.Bookstore"
          )";

class TranscoderAsyncBodyInjectionIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void SetUp() override {
    HttpProtocolIntegrationTest::SetUp();

    config_helper_.prependFilter(R"EOF(
    name: async-inject-body-at-end-stream-filter
    )EOF");
    config_helper_.prependFilter(absl::StrFormat(
        kTranscoderConfig, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
    config_helper_.prependFilter(R"EOF(
    name: async-inject-body-at-end-stream-filter
    )EOF");

    initialize();
  }

  template <class ResponseType>
  void encodeGrpcResponseMessages(const std::vector<std::string>& response_messages_txtpb) {
    for (const auto& response_message_txtpb : response_messages_txtpb) {
      ResponseType response_message;
      EXPECT_TRUE(TextFormat::ParseFromString(response_message_txtpb, &response_message));
      auto buffer = Grpc::Common::serializeToGrpcFrame(response_message);
      upstream_request_->encodeData(*buffer, false);
    }
  }
};

// Send request/response with NO trailers. Transcoding filter will
// pass through the request because strict validation options are not enabled.
TEST_P(TranscoderAsyncBodyInjectionIntegrationTest, HttpPassThroughNoTrailers) {
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send request with no trailers.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, "request_body");
  waitForNextUpstreamRequest();

  // Make sure that the body was properly propagated (with no modification).
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(upstream_request_->receivedData());
  EXPECT_EQ(upstream_request_->body().toString(), "request_body");

  // Send response with no trailers.
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData("response_body", true);
  ASSERT_TRUE(response->waitForEndStream());

  // Make sure that the body was properly propagated (with no modification).
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->body(), "response_body");
}

// Send simple HTTP request/response with trailers. Transcoding filter will
// pass through the request because strict validation options are not enabled.
TEST_P(TranscoderAsyncBodyInjectionIntegrationTest, HttpPassThroughWithTrailers) {
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send request with trailers.
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, "request_body", false);
  Http::TestRequestTrailerMapImpl request_trailers{{"trailer-key", "request"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();

  // Make sure that the body and trailers were properly propagated (with no modification).
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(upstream_request_->receivedData());
  EXPECT_EQ(upstream_request_->body().toString(), "request_body");
  ASSERT_TRUE(upstream_request_->trailers());
  EXPECT_EQ(upstream_request_->trailers()->size(), 1);

  // Send response with trailers.
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData("response_body", false);
  Http::TestResponseTrailerMapImpl response_trailers{{"trailer-key", "response"}};
  upstream_request_->encodeTrailers(response_trailers);
  ASSERT_TRUE(response->waitForEndStream());

  // Make sure that the body and trailers were properly propagated (with no modification).
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->body(), "response_body");
  ASSERT_TRUE(response->trailers());
  EXPECT_EQ(response->trailers()->size(), 1);
}

// Send HTTP request and unary gRPC response WITH trailers.
//
// Tests filter chain with multiple filters, including one that buffers on the
// response path (gRPC transcoder buffers entire body for unary response).
//
// Previously, this test would result in response (encoder) path failing with
// `upstream_reset_before_response_started{connection_termination}` because the
// transcoder kept buffered data in the `FilterManager` shared buffer. This
// conflicted with `async-inject-body-at-end-stream-filter` doing async
// continuation of trailers.
TEST_P(TranscoderAsyncBodyInjectionIntegrationTest, UnaryGrpcTranscoding) {
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send HTTP/JSON request (no trailers).
  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/shelf"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"content-type", "application/json"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, R"({"theme": "Children"})");
  waitForNextUpstreamRequest();

  // Make sure gRPC request was received.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(upstream_request_->receivedData());
  EXPECT_GT(upstream_request_->body().length(), 0);

  // Send gRPC response with trailers.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"content-type", "application/grpc"}};
  upstream_request_->encodeHeaders(response_headers, false);
  encodeGrpcResponseMessages<bookstore::Shelf>({R"(id: 20 theme: "Children")"});
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "0"}};
  upstream_request_->encodeTrailers(response_trailers);
  ASSERT_TRUE(response->waitForEndStream());

  // Make sure downstream received HTTP/JSON request.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->body(), R"({"id":"20","theme":"Children"})");
}

// Send HTTP request and streaming gRPC response WITH trailers.
TEST_P(TranscoderAsyncBodyInjectionIntegrationTest, StreamingGrpcTranscoding) {
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send HTTP/JSON request (no trailers).
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/shelves/1/books"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"content-type", "application/json"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, "");
  waitForNextUpstreamRequest();

  // Make sure gRPC request was received.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(upstream_request_->receivedData());
  EXPECT_GT(upstream_request_->body().length(), 0);

  // Send gRPC response with trailers.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"content-type", "application/grpc"}};
  upstream_request_->encodeHeaders(response_headers, false);

  // Two messages.
  encodeGrpcResponseMessages<bookstore::Book>({
      R"(id: 1 author: "Neal Stephenson" title: "Readme")",
      R"(id: 2 author: "George R.R. Martin" title: "A Game of Thrones")",
  });

  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "0"}};
  upstream_request_->encodeTrailers(response_trailers);
  ASSERT_TRUE(response->waitForEndStream());

  // Make sure downstream received HTTP/JSON request.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->body(),
            R"([{"id":"1","author":"Neal Stephenson","title":"Readme"})"
            R"(,{"id":"2","author":"George R.R. Martin","title":"A Game of Thrones"}])");
}

// We only test with HTTP/2 because these test cases involve trailers.
INSTANTIATE_TEST_SUITE_P(TestTranscoderChain, TranscoderAsyncBodyInjectionIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             /*downstream_protocols=*/{Http::CodecType::HTTP2},
                             /*upstream_protocols=*/{Http::CodecType::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace
} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
