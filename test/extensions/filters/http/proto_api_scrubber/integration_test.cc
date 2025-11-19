#include "envoy/grpc/status.h"

#include "test/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_test_lib.h"
#include "test/integration/http_protocol_integration.h"
#include "test/proto/apikeys.pb.h"

#include "fmt/format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {

using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::checkSerializedData;

std::string apikeysDescriptorPath() {
  return TestEnvironment::runfilesPath("test/proto/apikeys.descriptor");
}

const std::string kCreateApiKeyMethod = "/apikeys.ApiKeys/CreateApiKey";

// CEL Matcher Config: Evaluates to TRUE (Trigger Action)
const std::string kCelAlwaysTrue = R"EOF(
                                    cel_expr_parsed:
                                      expr:
                                        id: 1
                                        const_expr:
                                          bool_value: true
                                      source_info:
                                        syntax_version: "cel1"
                                        location: "inline_expression"
                                        positions:
                                          1: 0
)EOF";

// CEL Matcher Config: Evaluates to FALSE (Skip Action)
const std::string kCelAlwaysFalse = R"EOF(
                                    cel_expr_parsed:
                                      expr:
                                        id: 1
                                        const_expr:
                                          bool_value: false
                                      source_info:
                                        syntax_version: "cel1"
                                        location: "inline_expression"
                                        positions:
                                          1: 0
)EOF";

class ProtoApiScrubberIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void SetUp() override { HttpProtocolIntegrationTest::SetUp(); }

  void TearDown() override {
    cleanupUpstreamAndDownstream();
    HttpProtocolIntegrationTest::TearDown();
  }

  std::string getFilterConfig(const std::string& descriptor_path,
                              const std::string& method_name = "",
                              const std::string& field_to_scrub = "",
                              const std::string& cel_matcher_config = kCelAlwaysTrue) {

    std::string restrictions_yaml = "";

    if (!method_name.empty() && !field_to_scrub.empty()) {
      restrictions_yaml = fmt::format(
          R"EOF(
  restrictions:
    method_restrictions:
      "{0}":
        request_field_restrictions:
          "{1}":
            matcher:
              matcher_list:
                matchers:
                  - predicate:
                      single_predicate:
                        input:
                          name: "envoy.matching.inputs.cel_data_input"
                          typed_config:
                            "@type": type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput
                        custom_match:
                          name: "envoy.matching.matchers.cel_matcher"
                          typed_config:
                            "@type": type.googleapis.com/xds.type.matcher.v3.CelMatcher
                            expr_match:
                              {2}
                    on_match:
                      action:
                        name: remove_field
                        typed_config:
                          "@type": type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction
        )EOF",
          method_name, field_to_scrub, cel_matcher_config);
    }

    return fmt::format(
        R"EOF(
name: envoy.filters.http.proto_api_scrubber
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.ProtoApiScrubberConfig
  filtering_mode: OVERRIDE
  descriptor_set:
    data_source:
      filename: {0}
{1}
)EOF",
        descriptor_path, restrictions_yaml);
  }

  template <typename T>
  IntegrationStreamDecoderPtr sendGrpcRequest(const T& request_msg,
                                              const std::string& method_path) {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    auto request_buf = Grpc::Common::serializeToGrpcFrame(request_msg);

    auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                          {":path", method_path},
                                                          {"content-type", "application/grpc"},
                                                          {":authority", "host"},
                                                          {":scheme", "http"}};

    return codec_client_->makeRequestWithBody(request_headers, request_buf->toString());
  }
};

apikeys::CreateApiKeyRequest makeCreateApiKeyRequest(absl::string_view pb = R"pb(
  parent: "projects/123"
  key {
    display_name: "test-key"
    current_key: "abc-123"
  }
)pb") {
  apikeys::CreateApiKeyRequest request;
  Protobuf::TextFormat::ParseFromString(pb, &request);
  return request;
}

// ============================================================================
// TEST GROUP 1: PASS THROUGH
// ============================================================================

TEST_P(ProtoApiScrubberIntegrationTest, UnaryRequestPassesThrough) {
  config_helper_.prependFilter(getFilterConfig(apikeysDescriptorPath()));
  initialize();

  auto request_proto = makeCreateApiKeyRequest();

  auto response = sendGrpcRequest(request_proto, kCreateApiKeyMethod);
  waitForNextUpstreamRequest();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(upstream_request_->receivedData());

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<apikeys::CreateApiKeyRequest>(data, {request_proto});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

TEST_P(ProtoApiScrubberIntegrationTest, StreamingPassesThrough) {
  config_helper_.prependFilter(getFilterConfig(apikeysDescriptorPath()));
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto req1 = makeCreateApiKeyRequest(R"pb(parent: "req1")pb");
  auto req2 = makeCreateApiKeyRequest(R"pb(parent: "req2")pb");
  auto req3 = makeCreateApiKeyRequest(R"pb(parent: "req3")pb");

  Buffer::OwnedImpl combined_request;
  combined_request.move(*Grpc::Common::serializeToGrpcFrame(req1));
  combined_request.move(*Grpc::Common::serializeToGrpcFrame(req2));
  combined_request.move(*Grpc::Common::serializeToGrpcFrame(req3));

  auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                        {":path", kCreateApiKeyMethod},
                                                        {"content-type", "application/grpc"},
                                                        {":authority", "host"},
                                                        {":scheme", "http"}};

  auto response = codec_client_->makeRequestWithBody(request_headers, combined_request.toString());
  waitForNextUpstreamRequest();

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<apikeys::CreateApiKeyRequest>(data, {req1, req2, req3});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// ============================================================================
// TEST GROUP 2: SCRUBBING LOGIC
// ============================================================================

TEST_P(ProtoApiScrubberIntegrationTest, ScrubTopLevelField) {
  config_helper_.prependFilter(
      getFilterConfig(apikeysDescriptorPath(), kCreateApiKeyMethod, "parent"));
  initialize();

  auto original_proto = makeCreateApiKeyRequest(R"pb(
    parent: "sensitive-data"
    key { display_name: "public" }
  )pb");

  auto response = sendGrpcRequest(original_proto, kCreateApiKeyMethod);
  waitForNextUpstreamRequest();

  apikeys::CreateApiKeyRequest expected = original_proto;
  expected.clear_parent();

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<apikeys::CreateApiKeyRequest>(data, {expected});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

TEST_P(ProtoApiScrubberIntegrationTest, ScrubNestedField) {
  config_helper_.prependFilter(
      getFilterConfig(apikeysDescriptorPath(), kCreateApiKeyMethod, "key.display_name"));
  initialize();

  auto original_proto = makeCreateApiKeyRequest(R"pb(
    parent: "public"
    key { display_name: "sensitive" }
  )pb");

  auto response = sendGrpcRequest(original_proto, kCreateApiKeyMethod);
  waitForNextUpstreamRequest();

  apikeys::CreateApiKeyRequest expected = original_proto;
  expected.mutable_key()->clear_display_name();

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<apikeys::CreateApiKeyRequest>(data, {expected});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

TEST_P(ProtoApiScrubberIntegrationTest, ScrubConditionalFalse) {
  config_helper_.prependFilter(
      getFilterConfig(apikeysDescriptorPath(), kCreateApiKeyMethod, "parent", kCelAlwaysFalse));
  initialize();

  auto original_proto = makeCreateApiKeyRequest(R"pb(parent: "should-stay")pb");
  auto response = sendGrpcRequest(original_proto, kCreateApiKeyMethod);
  waitForNextUpstreamRequest();

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<apikeys::CreateApiKeyRequest>(data, {original_proto});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// ============================================================================
// TEST GROUP 3: VALIDATION & REJECTION
// ============================================================================

TEST_P(ProtoApiScrubberIntegrationTest, RejectsMethodNotInDescriptor) {
  config_helper_.prependFilter(getFilterConfig(apikeysDescriptorPath()));
  initialize();

  auto request_proto = makeCreateApiKeyRequest();
  auto response = sendGrpcRequest(request_proto, "/apikeys.ApiKeys/NonExistentMethod");

  ASSERT_TRUE(response->waitForEndStream());

  // For gRPC requests, Envoy returns HTTP 200 with grpc-status in the header.
  // We check that grpc-status matches INVALID_ARGUMENT (3).
  auto grpc_status = response->headers().GrpcStatus();
  ASSERT_TRUE(grpc_status != nullptr);
  EXPECT_EQ("3", grpc_status->value().getStringView()); // 3 = Invalid Argument
}

TEST_P(ProtoApiScrubberIntegrationTest, RejectsInvalidPathFormat) {
  config_helper_.prependFilter(getFilterConfig(apikeysDescriptorPath()));
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                        {":path", "/invalid-format"},
                                                        {"content-type", "application/grpc"},
                                                        {":authority", "host"},
                                                        {":scheme", "http"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());

  // For gRPC requests, expect HTTP 200 with grpc-status header.
  auto grpc_status = response->headers().GrpcStatus();
  ASSERT_TRUE(grpc_status != nullptr);
  EXPECT_EQ("3", grpc_status->value().getStringView()); // 3 = Invalid Argument
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
