#include <initializer_list>
#include <optional>

#include "envoy/common/optref.h"

#include "test/integration/http_protocol_integration.h"
#include "test/proto/apikeys.pb.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {

using ::apikeys::ApiKey;
using ::apikeys::CreateApiKeyRequest;

class ProtoApiScrubberIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void SetUp() override {
    HttpProtocolIntegrationTest::SetUp();

    config_helper_.prependFilter(filter());

    initialize();
  }

  void TearDown() override {
    cleanupUpstreamAndDownstream();
    HttpProtocolIntegrationTest::TearDown();
  }

  void sendResponse(IntegrationStreamDecoder* response, Envoy::Buffer::Instance* response_data) {
    Envoy::Http::TestResponseHeaderMapImpl resp_headers = Envoy::Http::TestResponseHeaderMapImpl{
        {":status", "200"},
        {"grpc-status", "1"},
        {"content-type", "application/grpc"},
    };
    upstream_request_->encodeHeaders(resp_headers, false);
    upstream_request_->encodeData(*response_data, true);
    ASSERT_TRUE(response->waitForEndStream());
  }

  std::string filter() {
    return absl::StrFormat(
        R"EOF(
name: proto_api_scrubber
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.ProtoApiScrubberConfig
  descriptor_set:
    data_source:
      filename: %s
)EOF",
        TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"));
  }
};

CreateApiKeyRequest makeCreateApiKeyRequest(absl::string_view pb = R"pb(
  parent: "project-id"
)pb") {
  CreateApiKeyRequest request;
  Protobuf::TextFormat::ParseFromString(pb, &request);
  return request;
}

apikeys::ApiKey makeCreateApiKeyResponse(absl::string_view pb = R"pb(
  name: "apikey-name"
  display_name: "Display Name"
  current_key: "current-key"
  create_time { seconds: 1684306560 nanos: 0 }
  update_time { seconds: 1684306560 nanos: 0 }
  location: "global"
  kms_key: "projects/my-project/locations/my-location"
  expire_time { seconds: 1715842560 nanos: 0 }
)pb") {
  apikeys::ApiKey response;
  Envoy::Protobuf::TextFormat::ParseFromString(pb, &response);
  return response;
}

// Tests that a unary request should passthrough the filter without any errors.
TEST_P(ProtoApiScrubberIntegrationTest, PassThroughUnary) {
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request = makeCreateApiKeyRequest();
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

  // Send response.
  ApiKey apikey_response = makeCreateApiKeyResponse();
  Envoy::Buffer::InstancePtr response_data =
      Envoy::Grpc::Common::serializeToGrpcFrame(apikey_response);
  sendResponse(response.get(), response_data.get());
}

INSTANTIATE_TEST_SUITE_P(Protocols, ProtoApiScrubberIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             /*downstream_protocols=*/{Http::CodecType::HTTP2},
                             /*upstream_protocols=*/{Http::CodecType::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
