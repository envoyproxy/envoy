#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.h"
#include "envoy/grpc/status.h"

#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib/field_checker.h"

#include "test/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_test_lib.h"
#include "test/extensions/filters/http/proto_api_scrubber/scrubber_test.pb.h"
#include "test/integration/http_protocol_integration.h"
#include "test/proto/apikeys.pb.h"

#include "cel/expr/syntax.pb.h"
#include "fmt/format.h"
#include "parser/parser.h"
#include "xds/type/matcher/v3/cel.pb.h"
#include "xds/type/matcher/v3/matcher.pb.h"
#include "xds/type/matcher/v3/string.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {

using envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig;
using envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter;
using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::checkSerializedData;

std::string apikeysDescriptorPath() {
  return TestEnvironment::runfilesPath("test/proto/apikeys.descriptor");
}

std::string scrubberTestDescriptorPath() {
  return TestEnvironment::runfilesPath(
      "test/extensions/filters/http/proto_api_scrubber/scrubber_test.descriptor");
}

const std::string kCreateApiKeyMethod = "/apikeys.ApiKeys/CreateApiKey";

class ProtoApiScrubberIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void SetUp() override { HttpProtocolIntegrationTest::SetUp(); }

  void TearDown() override {
    cleanupUpstreamAndDownstream();
    HttpProtocolIntegrationTest::TearDown();
  }

  enum class RestrictionType { Request, Response };

  static xds::type::matcher::v3::Matcher::MatcherList::Predicate
  buildCelPredicate(absl::string_view cel_expression) {
    // Parse the string into an AST.
    const cel::expr::ParsedExpr ast = *google::api::expr::parser::Parse(cel_expression);

    // Build the envoy matcher config.
    xds::type::matcher::v3::Matcher::MatcherList::Predicate predicate;
    auto* single = predicate.mutable_single_predicate();

    // Build CEL input and CEL matcher.
    single->mutable_input()->set_name("envoy.matching.inputs.cel_data_input");
    xds::type::matcher::v3::HttpAttributesCelMatchInput input_config;
    single->mutable_input()->mutable_typed_config()->PackFrom(input_config);
    auto* custom_match = single->mutable_custom_match();
    custom_match->set_name("envoy.matching.matchers.cel_matcher");
    xds::type::matcher::v3::CelMatcher cel_matcher;

    // Assign the parsed AST to the configuration and return the predicate.
    *cel_matcher.mutable_expr_match()->mutable_cel_expr_parsed() = ast;
    custom_match->mutable_typed_config()->PackFrom(cel_matcher);
    return predicate;
  }

  // Helper to build config with multiple fields to scrub and a generic predicate.
  std::string getMultiFieldFilterConfig(
      const std::string& descriptor_path, const std::string& method_name,
      const std::vector<std::string>& fields_to_scrub,
      RestrictionType type = RestrictionType::Request,
      const xds::type::matcher::v3::Matcher::MatcherList::Predicate& match_predicate =
          buildCelPredicate("true")) {

    ProtoApiScrubberConfig config;
    config.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);
    config.mutable_descriptor_set()->mutable_data_source()->set_filename(descriptor_path);

    if (!method_name.empty() && !fields_to_scrub.empty()) {
      auto* method_restrictions = config.mutable_restrictions()->mutable_method_restrictions();
      auto& method_config = (*method_restrictions)[method_name];
      auto* restrictions_map = (type == RestrictionType::Request)
                                   ? method_config.mutable_request_field_restrictions()
                                   : method_config.mutable_response_field_restrictions();

      // Create the Matcher object once.
      xds::type::matcher::v3::Matcher matcher_proto;
      auto* matcher_entry = matcher_proto.mutable_matcher_list()->add_matchers();
      *matcher_entry->mutable_predicate() = match_predicate;
      envoy::extensions::filters::http::proto_api_scrubber::v3::RemoveFieldAction remove_action;
      matcher_entry->mutable_on_match()->mutable_action()->mutable_typed_config()->PackFrom(
          remove_action);
      matcher_entry->mutable_on_match()->mutable_action()->set_name("remove_field");

      // Apply to all requested fields.
      for (const auto& field : fields_to_scrub) {
        *(*restrictions_map)[field].mutable_matcher() = matcher_proto;
      }
    }

    Protobuf::Any any_config;
    any_config.PackFrom(config);
    return fmt::format(R"EOF(
    name: envoy.filters.http.proto_api_scrubber
    typed_config: {})EOF",
                       MessageUtil::getJsonStringFromMessageOrError(any_config));
  }

  // Helper to build the configuration with a generic predicate.
  std::string
  getFilterConfig(const std::string& descriptor_path, const std::string& method_name = "",
                  const std::string& field_to_scrub = "",
                  RestrictionType type = RestrictionType::Request,
                  const xds::type::matcher::v3::Matcher::MatcherList::Predicate& match_predicate =
                      buildCelPredicate("true")) {
    std::vector<std::string> fields;
    if (!field_to_scrub.empty()) {
      fields.push_back(field_to_scrub);
    }
    return getMultiFieldFilterConfig(descriptor_path, method_name, fields, type, match_predicate);
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

INSTANTIATE_TEST_SUITE_P(Protocols, ProtoApiScrubberIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             /*downstream_protocols=*/{Http::CodecType::HTTP2},
                             /*upstream_protocols=*/{Http::CodecType::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

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

// Consolidated Map Scrubbing Test covering all map scenarios.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubAllMapTypes) {
  config_helper_.prependFilter(getMultiFieldFilterConfig(
      scrubberTestDescriptorPath(), "/scrubber_test.ScrubberTestService/Scrub",
      {
          "tags.value",                    // String to String map: value should be scrubbed.
          "int_map.value",                 // String to Int map: value should be scrubbed.
                                           // String to Int map: value kept (Implicit).
          "deep_map.value.secret",         // String to Object map: partial field scrubbed.
          "object_map.value.secret",       // String to Object map: all fields scrubbed (A).
          "object_map.value.public_field", // String to Object map: all fields scrubbed (B).
          "object_map.value.other_info",   // String to Object map: all fields scrubbed (C).
          "full_scrub_map.value"           // String to Object map: object itself scrubbed.
      },
      RestrictionType::Request, buildCelPredicate("true")));

  initialize();

  scrubber_test::ScrubRequest request;

  // String to String (Scrubbed).
  (*request.mutable_tags())["key_scrub"] = "secret";

  // String to Int (Scrubbed).
  (*request.mutable_int_map())["key_scrub"] = 123;

  // String to Int (Kept).
  (*request.mutable_safe_int_map())["key_safe"] = 456;

  // String to Object (Partial Scrub).
  // deep_map rules only target "secret". "public_field" and "other_info" are untouched.
  auto& partial = (*request.mutable_deep_map())["k_partial"];
  partial.set_secret("sensitive");
  partial.set_public_field("safe");

  // String to Object (All Fields Scrubbed -> Empty Message).
  // object_map rules target ALL fields ("secret", "public_field", "other_info").
  // The 'value' message itself is kept, but it becomes empty.
  auto& all_fields = (*request.mutable_object_map())["k_empty_res"];
  all_fields.set_secret("sensitive");
  all_fields.set_public_field("sensitive_too");
  all_fields.set_other_info("info");

  // String to Object (Object Scrubbed).
  // full_scrub_map rule targets "value" directly.
  auto& obj_scrub = (*request.mutable_full_scrub_map())["k_object"];
  obj_scrub.set_secret("sensitive");

  auto response = sendGrpcRequest(request, "/scrubber_test.ScrubberTestService/Scrub");
  waitForNextUpstreamRequest();

  // Verification.
  scrubber_test::ScrubRequest expected = request;

  // String to String: Value scrubbed -> Entry Removed.
  expected.mutable_tags()->erase("key_scrub");

  // String to Int: Value scrubbed -> Entry Removed.
  expected.mutable_int_map()->erase("key_scrub");

  // String to Int: Kept. (No change).

  // Partial Scrub: 'secret' removed. 'public_field' remains.
  auto& partial_exp = (*expected.mutable_deep_map())["k_partial"];
  partial_exp.set_secret("");
  // public_field remains "safe".

  // All Fields Scrubbed (Effectively).
  // All 3 fields scrubbed.
  // Result: Key + Empty Message -> Entry KEPT.
  auto& all_fields_exp = (*expected.mutable_object_map())["k_empty_res"];
  all_fields_exp.set_secret("");
  all_fields_exp.set_public_field("");
  all_fields_exp.set_other_info("");

  // Object Scrubbed: Value message excluded -> Entry REMOVED.
  expected.mutable_full_scrub_map()->erase("k_object");

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<scrubber_test::ScrubRequest>(data, {expected});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// ============================================================================
// TEST GROUP 1: PASS THROUGH
// ============================================================================

// Tests that the simple non-streaming request passes through without modification if there are no
// restrictions configured in the filter config.
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

// Tests that the streaming request passes through without modification if there are no restrictions
// configured in the filter config.
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

// Tests scrubbing of top level fields in the request when the corresponding matcher evaluates to
// true.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubTopLevelField) {
  config_helper_.prependFilter(getFilterConfig(apikeysDescriptorPath(), kCreateApiKeyMethod,
                                               "parent", RestrictionType::Request,
                                               buildCelPredicate("true")));
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

// Tests scrubbing of nested fields in the request when the corresponding matcher evaluates to true.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubNestedField_MatcherTrue) {
  config_helper_.prependFilter(getFilterConfig(apikeysDescriptorPath(), kCreateApiKeyMethod,
                                               "key.display_name", RestrictionType::Request,
                                               buildCelPredicate("true")));
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

// Tests scrubbing of nested fields in the request when the corresponding matcher evaluates to
// false.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubNestedField_MatcherFalse) {
  config_helper_.prependFilter(getFilterConfig(apikeysDescriptorPath(), kCreateApiKeyMethod,
                                               "key.display_name", RestrictionType::Request,
                                               buildCelPredicate("false")));
  initialize();

  auto original_proto = makeCreateApiKeyRequest(R"pb(
    parent: "public"
    key { display_name: "should-stay" }
  )pb");

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

// Tests that the request is rejected if the called gRPC method doesn't exist in the descriptor
// configured in the filter config.
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

// Tests that the request is rejected if the gRPC `:path` header is in invalid format.
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

} // namespace
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
