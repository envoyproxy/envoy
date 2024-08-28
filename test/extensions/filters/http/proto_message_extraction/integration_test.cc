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
namespace ProtoMessageExtraction {
namespace {

using ::apikeys::ApiKey;
using ::apikeys::CreateApiKeyRequest;
using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::checkSerializedData;

void compareJson(const std::string& actual, const std::string& expected) {
  ProtobufWkt::Value expected_value, actual_value;
  TestUtility::loadFromJson(expected, expected_value);
  TestUtility::loadFromJson(actual, actual_value);
  EXPECT_TRUE(TestUtility::protoEqual(expected_value, actual_value))
      << "got:\n"
      << actual_value.DebugString() << "\nexpected:\n"
      << expected_value.DebugString();
}

class IntegrationTest : public HttpProtocolIntegrationTest {
public:
  void SetUp() override {
    HttpProtocolIntegrationTest::SetUp();

    config_helper_.prependFilter(filter());
    useAccessLog("%DYNAMIC_METADATA(envoy.filters.http.proto_message_extraction)%");

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
name: proto_message_extraction
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.proto_message_extraction.v3.ProtoMessageExtractionConfig
  mode: FIRST_AND_LAST
  data_source:
    filename: %s
  extraction_by_method:
    apikeys.ApiKeys.CreateApiKey:
      request_extraction_by_field:
        parent: EXTRACT
      response_extraction_by_field:
        name: EXTRACT
    apikeys.ApiKeys.CreateApiKeyInStream:
      request_extraction_by_field:
        parent: EXTRACT
      response_extraction_by_field:
        name: EXTRACT
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
  ApiKey apikey_response = makeCreateApiKeyResponse();
  Envoy::Buffer::InstancePtr response_data =
      Envoy::Grpc::Common::serializeToGrpcFrame(apikey_response);
  sendResponse(response.get(), response_data.get());

  compareJson(waitForAccessLog(access_log_name_),
              R"(
{
  "requests": {
    "first": {
      "@type": "type.googleapis.com/apikeys.CreateApiKeyRequest",
      "parent": "project-id"
    }
  },
  "responses": {
    "first": {
      "@type": "type.googleapis.com/apikeys.ApiKey",
      "name": "apikey-name"
    }
  }
})");
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

  // Create streaming response and send it.
  ApiKey response1 = makeCreateApiKeyResponse(
      R"pb(
        name: "apikey-name-1"
        display_name: "Display Name"
        current_key: "current-key"
        create_time { seconds: 1684306560 nanos: 0 }
        update_time { seconds: 1684306560 nanos: 0 }
        location: "global"
        kms_key: "projects/my-project/locations/my-location"
        expire_time { seconds: 1715842560 nanos: 0 }
      )pb");
  Envoy::Buffer::InstancePtr response_data1 = Envoy::Grpc::Common::serializeToGrpcFrame(response1);
  ApiKey response2 = makeCreateApiKeyResponse(
      R"pb(
        name: "apikey-name-2"
        display_name: "Display Name"
        current_key: "current-key"
        create_time { seconds: 1684306560 nanos: 0 }
        update_time { seconds: 1684306560 nanos: 0 }
        location: "global"
        kms_key: "projects/my-project/locations/my-location"
        expire_time { seconds: 1715842560 nanos: 0 }
      )pb");
  Envoy::Buffer::InstancePtr response_data2 = Envoy::Grpc::Common::serializeToGrpcFrame(response2);
  ApiKey response3 = makeCreateApiKeyResponse(
      R"pb(
        name: "apikey-name-3"
        display_name: "Display Name"
        current_key: "current-key"
        create_time { seconds: 1684306560 nanos: 0 }
        update_time { seconds: 1684306560 nanos: 0 }
        location: "global"
        kms_key: "projects/my-project/locations/my-location"
        expire_time { seconds: 1715842560 nanos: 0 }
      )pb");
  Envoy::Buffer::InstancePtr response_data3 = Envoy::Grpc::Common::serializeToGrpcFrame(response3);

  // Split into multiple buffers.
  Envoy::Buffer::OwnedImpl response_data;
  response_data.move(*response_data1);
  response_data.move(*response_data2);
  response_data.move(*response_data3);
  EXPECT_EQ(response_data1->length(), 0);
  EXPECT_EQ(response_data2->length(), 0);
  EXPECT_EQ(response_data3->length(), 0);

  sendResponse(response.get(), &response_data);

  // Inject data back and no data modification.
  checkSerializedData<apikeys::ApiKey>(response_data, {response1, response2, response3});

  compareJson(waitForAccessLog(access_log_name_), R"(
{
  "responses": {
    "first": {
      "name": "apikey-name-1",
      "@type": "type.googleapis.com/apikeys.ApiKey"
    },
    "last": {
      "name": "apikey-name-3",
      "@type": "type.googleapis.com/apikeys.ApiKey"
    }
  },
  "requests": {
    "last": {
      "@type": "type.googleapis.com/apikeys.CreateApiKeyRequest",
      "parent": "from-req3"
    },
    "first": {
      "@type": "type.googleapis.com/apikeys.CreateApiKeyRequest",
      "parent": "from-req1"
    }
  }
})");
}

INSTANTIATE_TEST_SUITE_P(Protocols, IntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             /*downstream_protocols=*/{Http::CodecType::HTTP2},
                             /*upstream_protocols=*/{Http::CodecType::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace
} // namespace ProtoMessageExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
