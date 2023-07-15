#include <initializer_list>
#include <optional>

#include "envoy/common/optref.h"

#include "test/integration/http_protocol_integration.h"
#include "test/proto/apikeys.pb.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {
namespace {
using ::apikeys::ApiKey;
using ::apikeys::CreateApiKeyRequest;

class IntegrationTest : public HttpProtocolIntegrationTest {
public:
  void SetUp() override {
    HttpProtocolIntegrationTest::SetUp();

    config_helper_.prependFilter(filter());
    useAccessLog("%DYNAMIC_METADATA(envoy.filters.http.grpc_field_extraction)%");

    initialize();
  }

  void TearDown() override {
    cleanupUpstreamAndDownstream();
    HttpProtocolIntegrationTest::TearDown();
  }

  std::string filter() {
    return absl::StrFormat(R"EOF(
name: grpc_field_extraction
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_field_extraction.v3.GrpcFieldExtractionConfig
  descriptor_set:
    filename: %s
  extractions_by_method:
    apikeys.ApiKeys.CreateApiKey:
      request_field_extractions:
        parent:
)EOF",
                           TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"));
  }
};

CreateApiKeyRequest MakeCreateApiKeyRequest(absl::string_view pb = R"pb(
      parent: "project-id"
    )pb") {
  CreateApiKeyRequest request;
  Protobuf::TextFormat::ParseFromString(pb, &request);
  return request;
}

TEST_P(IntegrationTest, SunnyPath) {
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request = MakeCreateApiKeyRequest();
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);
  auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                        {":path", "/apikeys.ApiKeys/CreateApiKey"},
                                                        {"content-type", "application/grpc"},
                                                        {":authority", "host"},
                                                        {":scheme", "http"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, request_data->toString());
  waitForNextUpstreamRequest();

  // Make sure that the body was properly propagated (with no modification).
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(upstream_request_->receivedData());
  EXPECT_EQ(upstream_request_->body().toString(), request_data->toString());

  // Send response with no trailers.
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData("response_body", true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(waitForAccessLog(access_log_name_), R"({"parent":["project-id"]})");
}
INSTANTIATE_TEST_SUITE_P(Protocols, IntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             /*downstream_protocols=*/{Http::CodecType::HTTP2},
                             /*upstream_protocols=*/{Http::CodecType::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace
} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
