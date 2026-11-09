#include <initializer_list>
#include <optional>

#include "envoy/common/optref.h"

#include "test/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_test_lib.h"
#include "test/integration/http_protocol_integration.h"
#include "test/proto/apikeys.pb.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {
namespace {
using ::apikeys::CreateApiKeyRequest;

std::string grpcFieldExtractionFilterConfig() {
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
        key.name:
          metadata_key: api_key_name
    apikeys.ApiKeys.CreateApiKeyInStream:
      request_field_extractions:
        parent:
        key.name:
          metadata_key: api_key_name
)EOF",
                         TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"));
}

class IntegrationTest : public HttpProtocolIntegrationTest {
public:
  void SetUp() override {
    HttpProtocolIntegrationTest::SetUp();

    config_helper_.prependFilter(grpcFieldExtractionFilterConfig());
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
};

CreateApiKeyRequest makeCreateApiKeyRequest(absl::string_view pb = R"pb(
      parent: "project-id"
      key { name: "api-key-name" }
    )pb") {
  CreateApiKeyRequest request;
  std::ignore = Protobuf::TextFormat::ParseFromString(pb, &request);
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

  EXPECT_THAT(waitForAccessLog(access_log_name_),
              testing::AllOf(testing::HasSubstr(R"("parent":["project-id"])"),
                             testing::HasSubstr(R"("api_key_name":["api-key-name"])")));
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
      key { name: "key-from-req1" }
)pb");
  Envoy::Buffer::InstancePtr request_data1 = Envoy::Grpc::Common::serializeToGrpcFrame(request1);
  CreateApiKeyRequest request2 = makeCreateApiKeyRequest(
      R"pb(
      parent: "from-req2"
      key { name: "key-from-req2" }
)pb");
  Envoy::Buffer::InstancePtr request_data2 = Envoy::Grpc::Common::serializeToGrpcFrame(request2);
  CreateApiKeyRequest request3 = makeCreateApiKeyRequest(
      R"pb(
      parent: "from-req3"
      key { name: "key-from-req3" }
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
  Buffer::OwnedImpl body = upstream_request_->body();
  checkSerializedData<CreateApiKeyRequest>(body, {request1, request2, request3});

  // Send response.
  sendResponse(response.get());

  EXPECT_THAT(waitForAccessLog(access_log_name_),
              testing::AllOf(testing::HasSubstr(R"("parent":["from-req1"])"),
                             testing::HasSubstr(R"("api_key_name":["key-from-req1"])")));
}

INSTANTIATE_TEST_SUITE_P(Protocols, IntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             /*downstream_protocols=*/{Http::CodecType::HTTP2},
                             /*upstream_protocols=*/{Http::CodecType::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

class RawFrameIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                public Http2RawFrameIntegrationTest {
public:
  RawFrameIntegrationTest() : Http2RawFrameIntegrationTest(GetParam()) {}

  void SetUp() override {
    config_helper_.prependFilter(grpcFieldExtractionFilterConfig());
    useAccessLog("%DYNAMIC_METADATA(envoy.filters.http.grpc_field_extraction)%");
  }
};

// The DATA frame completing message 1 also carries the head of pipelined
// message 2 (its 5-byte gRPC delimiter and first payload bytes). The filter
// must forward those head bytes; otherwise the upstream receives message 2's
// payload without its delimiter and the stream is corrupted.
TEST_P(RawFrameIntegrationTest, StreamingPipelinedMessageHeadForwarded) {
  beginSession();

  const uint32_t stream_id = Http2Frame::makeClientStreamId(0);
  sendFrame(
      Http2Frame::makePostRequest(stream_id, "host", "/apikeys.ApiKeys/CreateApiKeyInStream",
                                  {{"content-type", "application/grpc"}, {"te", "trailers"}}));

  CreateApiKeyRequest request1 = makeCreateApiKeyRequest(
      R"pb(
      parent: "from-req1"
      key { name: "key-from-req1" }
)pb");
  const std::string message1 = Envoy::Grpc::Common::serializeToGrpcFrame(request1)->toString();
  CreateApiKeyRequest request2 = makeCreateApiKeyRequest(
      R"pb(
      parent: "from-req2"
      key { name: "key-from-req2" }
)pb");
  const std::string message2 = Envoy::Grpc::Common::serializeToGrpcFrame(request2)->toString();

  constexpr size_t kMessage2HeadSize = 12;
  sendFrame(Http2Frame::makeDataFrame(
      stream_id, absl::StrCat(message1, message2.substr(0, kMessage2HeadSize))));
  sendFrame(Http2Frame::makeDataFrame(stream_id, message2.substr(kMessage2HeadSize),
                                      Http2Frame::DataFlags::EndStream));

  waitForNextUpstreamConnection({0}, TestUtility::DefaultTimeout, fake_upstream_connection_);
  FakeStreamPtr upstream_request;
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request));
  ASSERT_TRUE(upstream_request->waitForEndStream(*dispatcher_));
  ASSERT_TRUE(upstream_request->receivedData());

  Buffer::OwnedImpl body = upstream_request->body();
  checkSerializedData<CreateApiKeyRequest>(body, {request1, request2});

  upstream_request->encodeHeaders(default_response_headers_, true);
  EXPECT_EQ(Http2Frame::Type::Headers, readFrame().type());
  tcp_client_->close();

  EXPECT_THAT(waitForAccessLog(access_log_name_),
              testing::AllOf(testing::HasSubstr(R"("parent":["from-req1"])"),
                             testing::HasSubstr(R"("api_key_name":["key-from-req1"])")));
}

INSTANTIATE_TEST_SUITE_P(IpVersions, RawFrameIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace
} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
