#include <initializer_list>
#include <optional>

#include "envoy/common/optref.h"

#include "test/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_test_lib.h"
#include "test/integration/http_protocol_integration.h"
#include "test/proto/apikeys.pb.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {
namespace {
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

  void sendResponse(IntegrationStreamDecoder* response) {
    upstream_request_->encodeHeaders(default_response_headers_, false);
    upstream_request_->encodeData("response_body", true);
    ASSERT_TRUE(response->waitForEndStream());
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
    apikeys.ApiKeys.CreateApiKeyInStream:
      request_field_extractions:
        parent:
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

TEST_P(IntegrationTest, Unary) {
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
  sendResponse(response.get());

  EXPECT_THAT(waitForAccessLog(access_log_name_), R"({"parent":["project-id"]})");
}

TEST_P(IntegrationTest, Streaming) {
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_headers =
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/apikeys.ApiKeys/CreateApiKeyInStream"},
                                     {"content-type", "application/grpc"},
                                     {":authority", "host"},
                                     {":scheme", "http"}};

  CreateApiKeyRequest request1 = makeCreateApiKeyRequest(
      R"pb(
      parent: "from-req1"
)pb");
  Envoy::Buffer::InstancePtr request_data1 = Envoy::Grpc::Common::serializeToGrpcFrame(request1);
  CreateApiKeyRequest request2 = makeCreateApiKeyRequest(
      R"pb(
      parent: "from-req2"
)pb");
  Envoy::Buffer::InstancePtr request_data2 = Envoy::Grpc::Common::serializeToGrpcFrame(request2);
  CreateApiKeyRequest request3 = makeCreateApiKeyRequest(
      R"pb(
      parent: "from-req3"
)pb");
  Envoy::Buffer::InstancePtr request_data3 = Envoy::Grpc::Common::serializeToGrpcFrame(request3);

  // Split into multiple buffers.
  Envoy::Buffer::OwnedImpl request_data;
  request_data.move(*request_data1);
  request_data.move(*request_data2);
  request_data.move(*request_data3);
  EXPECT_EQ(request_data1->length(), 0);
  EXPECT_EQ(request_data2->length(), 0);
  EXPECT_EQ(request_data3->length(), 0);

  auto response = codec_client_->makeRequestWithBody(request_headers, request_data.toString());
  waitForNextUpstreamRequest();

  // Make sure that the body was properly propagated (with no modification).
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(upstream_request_->receivedData());
  checkSerializedData<CreateApiKeyRequest>(upstream_request_->body(),
                                           {request1, request2, request3});

  // Send response.
  sendResponse(response.get());

  EXPECT_THAT(waitForAccessLog(access_log_name_), R"({"parent":["from-req1"]})");
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
