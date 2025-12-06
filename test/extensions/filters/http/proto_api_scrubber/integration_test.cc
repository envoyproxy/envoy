#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.h"
#include "envoy/grpc/status.h"

#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib/field_checker.h"

#include "test/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_test_lib.h"
#include "test/integration/http_protocol_integration.h"
#include "test/proto/apikeys.pb.h"
#include "test/proto/bookstore.pb.h"

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
std::string bookstoreDescriptorPath() {
  return TestEnvironment::runfilesPath("test/proto/bookstore.descriptor");
}

const std::string kCreateApiKeyMethod = "/apikeys.ApiKeys/CreateApiKey";
const std::string kCreateShelfMethod = "/bookstore.Bookstore/CreateShelf";

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

  xds::type::matcher::v3::Matcher
  buildMatcher(const xds::type::matcher::v3::Matcher::MatcherList::Predicate& match_predicate) {
    xds::type::matcher::v3::Matcher matcher_proto;
    auto* matcher_entry = matcher_proto.mutable_matcher_list()->add_matchers();
    *matcher_entry->mutable_predicate() = match_predicate;
    envoy::extensions::filters::http::proto_api_scrubber::v3::RemoveFieldAction remove_action;
    matcher_entry->mutable_on_match()->mutable_action()->mutable_typed_config()->PackFrom(
        remove_action);
    matcher_entry->mutable_on_match()->mutable_action()->set_name("remove_field");
    return matcher_proto;
  }

  // Helper to build the configuration for field-level restrictions.
  std::string
  getFilterConfig(const std::string& descriptor_path, const std::string& method_name = "",
                  const std::string& field_to_scrub = "",
                  RestrictionType type = RestrictionType::Request,
                  const xds::type::matcher::v3::Matcher::MatcherList::Predicate& match_predicate =
                      buildCelPredicate("true")) {
    ProtoApiScrubberConfig filter_config_proto;
    filter_config_proto.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);
    filter_config_proto.mutable_descriptor_set()->mutable_data_source()->set_filename(
        descriptor_path);

    if (!method_name.empty() && !field_to_scrub.empty()) {
      std::string restriction_key = (type == RestrictionType::Request)
                                        ? "request_field_restrictions"
                                        : "response_field_restrictions";

      xds::type::matcher::v3::Matcher matcher_proto = buildMatcher(match_predicate);

      auto& method_restrictions =
          (*filter_config_proto.mutable_restrictions()->mutable_method_restrictions())[method_name];
      auto* field_map = (type == RestrictionType::Request)
                            ? method_restrictions.mutable_request_field_restrictions()
                            : method_restrictions.mutable_response_field_restrictions();
      (*field_map)[field_to_scrub] =
          envoy::extensions::filters::http::proto_api_scrubber::v3::RestrictionConfig();
      *(*field_map)[field_to_scrub].mutable_matcher() = matcher_proto;
    }

    Protobuf::Any any_config;
    any_config.PackFrom(filter_config_proto);
    return fmt::format(R"EOF(
    name: envoy.filters.http.proto_api_scrubber
    typed_config: {})EOF",
                       MessageUtil::getJsonStringFromMessageOrError(any_config));
  }

  // Helper to build the configuration for message-level restrictions.
  std::string getConfigWithMessageRestriction(
      const std::string& descriptor_path, const std::string& message_name,
      const xds::type::matcher::v3::Matcher::MatcherList::Predicate& match_predicate =
          buildCelPredicate("true")) {
    ProtoApiScrubberConfig filter_config_proto;
    filter_config_proto.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);
    filter_config_proto.mutable_descriptor_set()->mutable_data_source()->set_filename(
        descriptor_path);

    if (!message_name.empty()) {
      xds::type::matcher::v3::Matcher matcher_proto = buildMatcher(match_predicate);
      auto& message_restrictions =
          (*filter_config_proto.mutable_restrictions()->mutable_message_restrictions());
      message_restrictions[message_name] =
          envoy::extensions::filters::http::proto_api_scrubber::v3::MessageRestrictions();
      *message_restrictions[message_name].mutable_config()->mutable_matcher() = matcher_proto;
    }

    Protobuf::Any any_config;
    any_config.PackFrom(filter_config_proto);
    return fmt::format(R"EOF(
    name: envoy.filters.http.proto_api_scrubber
    typed_config: {})EOF",
                       MessageUtil::getJsonStringFromMessageOrError(any_config));
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

bookstore::CreateShelfRequest makeCreateShelfRequest(absl::string_view pb = R"pb(
  shelf {
    id: 123
    theme: "Duckie's Favorites"
    internal_notes: "Ssshhh secret"
  }
)pb") {
  bookstore::CreateShelfRequest request;
  Protobuf::TextFormat::ParseFromString(pb, &request);
  return request;
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
// TEST GROUP 2: FIELD-LEVEL SCRUBBING LOGIC
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
// TEST GROUP 3: MESSAGE-LEVEL SCRUBBING LOGIC (CURRENT BEHAVIOR)
// ============================================================================

// Tests that message fields are NOT cleared when message-level restriction matches.
// kExclude from CheckType currently only prevents field traversal.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubMessageLevel_MatcherTrue) {
  config_helper_.prependFilter(getConfigWithMessageRestriction(
      bookstoreDescriptorPath(), "bookstore.Shelf", buildCelPredicate("true")));
  initialize();

  auto original_proto = makeCreateShelfRequest(); // Contains id, theme, internal_notes

  auto response = sendGrpcRequest(original_proto, kCreateShelfMethod);
  waitForNextUpstreamRequest();

  // EXPECTED BEHAVIOR (CURRENT LIMITATION): Message fields are NOT cleared.
  bookstore::CreateShelfRequest expected = original_proto;

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<bookstore::CreateShelfRequest>(data, {expected});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// Tests that a message is NOT scrubbed if the message-level restriction matcher is false.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubMessageLevel_MatcherFalse) {
  config_helper_.prependFilter(getConfigWithMessageRestriction(
      bookstoreDescriptorPath(), "bookstore.Shelf", buildCelPredicate("false")));
  initialize();

  auto original_proto = makeCreateShelfRequest();

  auto response = sendGrpcRequest(original_proto, kCreateShelfMethod);
  waitForNextUpstreamRequest();

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  // No changes expected
  checkSerializedData<bookstore::CreateShelfRequest>(data, {original_proto});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// Tests message-level restriction based on a request header.
// kExclude from CheckType currently only prevents field traversal.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubMessageLevel_HeaderMatch) {
  config_helper_.prependFilter(getConfigWithMessageRestriction(
      bookstoreDescriptorPath(), "bookstore.Shelf",
      buildCelPredicate("request.headers['x-scrub-shelf'] == 'true'")));
  initialize();

  auto original_proto = makeCreateShelfRequest();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_buf = Grpc::Common::serializeToGrpcFrame(original_proto);
  auto request_headers = Http::TestRequestHeaderMapImpl{
      {":method", "POST"},    {":path", kCreateShelfMethod}, {"content-type", "application/grpc"},
      {":authority", "host"}, {":scheme", "http"},           {"x-scrub-shelf", "true"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, request_buf->toString());
  waitForNextUpstreamRequest();

  // EXPECTED BEHAVIOR (CURRENT LIMITATION): Message fields are NOT cleared.
  bookstore::CreateShelfRequest expected = original_proto;

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<bookstore::CreateShelfRequest>(data, {expected});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// Tests precedence: Message-level kExclude prevents field-level rules from being applied
// because traversal into the message is stopped.
TEST_P(ProtoApiScrubberIntegrationTest, MessageScrubPrecedence) {
  // Config with both message and field level restrictions.
  ProtoApiScrubberConfig filter_config_proto;
  filter_config_proto.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);
  filter_config_proto.mutable_descriptor_set()->mutable_data_source()->set_filename(
      bookstoreDescriptorPath());

  // Message-level: Match bookstore.Shelf if header x-scrub-shelf is true.
  xds::type::matcher::v3::Matcher msg_matcher =
      buildMatcher(buildCelPredicate("request.headers['x-scrub-shelf'] == 'true'"));
  auto& message_restrictions =
      (*filter_config_proto.mutable_restrictions()->mutable_message_restrictions());
  message_restrictions["bookstore.Shelf"] =
      envoy::extensions::filters::http::proto_api_scrubber::v3::MessageRestrictions();
  *message_restrictions["bookstore.Shelf"].mutable_config()->mutable_matcher() = msg_matcher;

  // Field-level: Scrub shelf.internal_notes if header x-scrub-notes is true.
  xds::type::matcher::v3::Matcher field_matcher =
      buildMatcher(buildCelPredicate("request.headers['x-scrub-notes'] == 'true'"));
  auto& method_restrictions = (*filter_config_proto.mutable_restrictions()
                                    ->mutable_method_restrictions())[kCreateShelfMethod];
  auto* field_map = method_restrictions.mutable_request_field_restrictions();
  (*field_map)["shelf.internal_notes"] =
      envoy::extensions::filters::http::proto_api_scrubber::v3::RestrictionConfig();
  *(*field_map)["shelf.internal_notes"].mutable_matcher() = field_matcher;

  Protobuf::Any any_config;
  any_config.PackFrom(filter_config_proto);
  config_helper_.prependFilter(
      fmt::format(R"EOF(
    name: envoy.filters.http.proto_api_scrubber
    typed_config: {})EOF",
                  MessageUtil::getJsonStringFromMessageOrError(any_config)));
  initialize();

  auto original_proto = makeCreateShelfRequest(); // Has id, theme, internal_notes

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_buf = Grpc::Common::serializeToGrpcFrame(original_proto);
  // Set headers to trigger BOTH matchers
  auto request_headers = Http::TestRequestHeaderMapImpl{
      {":method", "POST"},      {":path", kCreateShelfMethod}, {"content-type", "application/grpc"},
      {":authority", "host"},   {":scheme", "http"},           {"x-scrub-shelf", "true"},
      {"x-scrub-notes", "true"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, request_buf->toString());
  waitForNextUpstreamRequest();

  // EXPECTED BEHAVIOR (CURRENT LIMITATION):
  // Message-level kExclude prevents field-level rules from running on shelf.* fields.
  // So, shelf.internal_notes is NOT scrubbed, because CheckField is never called for it.
  // The message itself is NOT cleared.
  bookstore::CreateShelfRequest expected = original_proto;

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<bookstore::CreateShelfRequest>(data, {expected});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// ============================================================================
// TEST GROUP 4: VALIDATION & REJECTION
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
