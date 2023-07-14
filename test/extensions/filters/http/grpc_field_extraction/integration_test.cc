#include <initializer_list>
#include <optional>

#include "envoy/common/optref.h"

#include "absl/strings/str_format.h"
#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {
namespace {

class IntegrationTest : public HttpProtocolIntegrationTest {
public:
  void SetUp() override { useAccessLog("%DYNAMIC_METADATA(envoy.filters.http.grpc_field_extraction)%"); }

  void TearDown() override {
    cleanupUpstreamAndDownstream();
    HttpProtocolIntegrationTest::TearDown();
  }

  void initializeFilter(const std::string& config) {
    config_helper_.prependFilter(config);
    initialize();
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  }

  IntegrationStreamDecoderPtr sendHeaderOnlyRequestAwaitResponse(
      const Http::TestRequestHeaderMapImpl& headers,
      std::function<void()> simulate_upstream = []() {}) {
    IntegrationStreamDecoderPtr response_decoder = codec_client_->makeHeaderOnlyRequest(headers);
    simulate_upstream();
    // Wait for the response to be read by the codec client.
    EXPECT_TRUE(response_decoder->waitForEndStream());
    EXPECT_TRUE(response_decoder->complete());
    return response_decoder;
  }



  std::string filter() {
    return absl::StrFormat(R"EOF(
name: grpc_field_extraction
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_field_extraction.v3.GrpcFieldExtractionConfig
  descriptor_set : {filename: %s}
)EOF", TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"));
  }

  OptRef<const Http::TestResponseTrailerMapImpl> empty_trailers_;
};

// TODO(#26236): Fix test suite for HTTP/3.
INSTANTIATE_TEST_SUITE_P(
    Protocols, IntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(IntegrationTest, SunnyPath) {
  initializeFilter(filter());

  // Include test name and params in URL to make each test's requests unique.
  ;


      auto encoder_decoder =
        codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}});
    request_encoder_ = &encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    codec_client_->sendData(*request_encoder_, 1, false);
    codec_client_->sendTrailers(*request_encoder_, request_trailers);
    waitForNextUpstreamRequest();

    // Sending back non gRPC-Web response.
    if (remove_content_type) {
      default_response_headers_.removeContentType();
    } else {
      default_response_headers_.setReferenceContentType(
          Http::Headers::get().ContentTypeValues.Json);
    }
    upstream_request_->encodeHeaders(default_response_headers_, /*end_stream=*/false);
    upstream_request_->encodeData(start, /*end_stream=*/false);
    upstream_request_->encodeData(end, /*end_stream=*/true);
    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(response->complete());
    EXPECT_EQ(expected, response->headers().getGrpcMessageValue());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(absl::StrCat(accept_, "+proto"), response->headers().getContentTypeValue());
    EXPECT_EQ(0U, response->body().length());
    codec_client_->close();

    const std::string response_body(10, 'a');
     Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"grpc-status", "0"}};


}

} // namespace
} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
