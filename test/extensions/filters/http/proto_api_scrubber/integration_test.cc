#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib/field_checker.h"

#include "test/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_test_lib.h"
#include "test/extensions/filters/http/proto_api_scrubber/scrubber_test.pb.h"
#include "test/integration/http_protocol_integration.h"
#include "test/proto/apikeys.pb.h"
#include "test/test_common/registry.h"

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

namespace scrubber_test = test::extensions::filters::http::proto_api_scrubber;

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
const std::string kFilterStateLabelKey = "filter_state_label_key";
const std::string kFilterStateLabelValue = "LABEL1,LABEL2,LABEL3";

// This filter injects data into filter_state.
class MetadataInjectorFilter : public ::Envoy::Http::PassThroughDecoderFilter {
public:
  ::Envoy::Http::FilterHeadersStatus decodeHeaders(::Envoy::Http::RequestHeaderMap&,
                                                   bool) override {
    const std::string key = kFilterStateLabelKey;
    const std::string value = kFilterStateLabelValue;
    decoder_callbacks_->streamInfo().filterState()->setData(
        key, std::make_shared<::Envoy::Router::StringAccessorImpl>(value),
        ::Envoy::StreamInfo::FilterState::StateType::ReadOnly);
    return ::Envoy::Http::FilterHeadersStatus::Continue;
  }
};

class MetadataInjectorConfigFactory
    : public ::Envoy::Server::Configuration::NamedHttpFilterConfigFactory {
public:
  absl::StatusOr<::Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProto(const ::Envoy::Protobuf::Message&, const std::string&,
                               ::Envoy::Server::Configuration::FactoryContext&) override {
    return [](::Envoy::Http::FilterChainFactoryCallbacks& callbacks) {
      callbacks.addStreamDecoderFilter(std::make_shared<MetadataInjectorFilter>());
    };
  }

  ::Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override { return "test_injector"; }
};

// RegisterFactory handles the singleton lifetime automatically and avoids the destruction crash.
static ::Envoy::Registry::RegisterFactory<
    MetadataInjectorConfigFactory, ::Envoy::Server::Configuration::NamedHttpFilterConfigFactory>
    register_test_injector;

class ProtoApiScrubberIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void SetUp() override { HttpProtocolIntegrationTest::SetUp(); }

  void TearDown() override {
    if (codec_client_) {
      // Close the client FIRST to prevent any "connection reset" callbacks.
      codec_client_->close();
      codec_client_.reset();
    }

    cleanupUpstreamAndDownstream();
    HttpProtocolIntegrationTest::TearDown();
  }

  enum class RestrictionType { Request, Response };
  enum class StringMatchType { Exact, Regex };

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

  static xds::type::matcher::v3::Matcher::MatcherList::Predicate
  buildStringMatcherPredicate(const std::string& input_extension_name,
                              const Protobuf::Message& input_config,
                              const std::string& match_pattern, StringMatchType match_type) {
    xds::type::matcher::v3::Matcher::MatcherList::Predicate predicate;
    auto* single = predicate.mutable_single_predicate();

    // Configure the Data Input (The source of the string to be matched)
    single->mutable_input()->set_name(input_extension_name);
    single->mutable_input()->mutable_typed_config()->PackFrom(input_config);

    // Configure the String Matcher (The logic to apply)
    auto* string_matcher = single->mutable_value_match();
    if (match_type == StringMatchType::Regex) {
      auto* regex = string_matcher->mutable_safe_regex();
      regex->mutable_google_re2();
      regex->set_regex(match_pattern);
    } else {
      string_matcher->set_exact(match_pattern);
    }

    return predicate;
  }

  // Helper to build config with multiple fields to scrub.
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

  // Helper to build config with message-level field restrictions.
  std::string getMessageLevelFilterConfig(
      const std::string& descriptor_path, const std::string& message_type,
      const std::string& field_to_scrub,
      const xds::type::matcher::v3::Matcher::MatcherList::Predicate& match_predicate =
          buildCelPredicate("true")) {

    ProtoApiScrubberConfig config;
    config.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);
    config.mutable_descriptor_set()->mutable_data_source()->set_filename(descriptor_path);

    if (!message_type.empty() && !field_to_scrub.empty()) {
      auto* message_restrictions = config.mutable_restrictions()->mutable_message_restrictions();
      auto& message_config = (*message_restrictions)[message_type];

      // Create the Matcher object.
      xds::type::matcher::v3::Matcher matcher_proto;
      auto* matcher_entry = matcher_proto.mutable_matcher_list()->add_matchers();
      *matcher_entry->mutable_predicate() = match_predicate;
      envoy::extensions::filters::http::proto_api_scrubber::v3::RemoveFieldAction remove_action;
      matcher_entry->mutable_on_match()->mutable_action()->mutable_typed_config()->PackFrom(
          remove_action);
      matcher_entry->mutable_on_match()->mutable_action()->set_name("remove_field");

      // Apply to the specific field in the message.
      *(*message_config.mutable_field_restrictions())[field_to_scrub].mutable_matcher() =
          matcher_proto;
    }

    Protobuf::Any any_config;
    any_config.PackFrom(config);
    return fmt::format(R"EOF(
    name: envoy.filters.http.proto_api_scrubber
    typed_config: {})EOF",
                       MessageUtil::getJsonStringFromMessageOrError(any_config));
  }

  // Helper to build config with global message-level restrictions.
  // This targets: restrictions.message_restrictions[type].config.matcher
  std::string getGlobalTypeFilterConfig(
      const std::string& descriptor_path, const std::string& type_name,
      const xds::type::matcher::v3::Matcher::MatcherList::Predicate& match_predicate =
          buildCelPredicate("true")) {

    ProtoApiScrubberConfig config;
    config.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);
    config.mutable_descriptor_set()->mutable_data_source()->set_filename(descriptor_path);

    auto* message_restrictions = config.mutable_restrictions()->mutable_message_restrictions();
    auto& message_config = (*message_restrictions)[type_name];

    // Create the Matcher object for the Type itself.
    xds::type::matcher::v3::Matcher matcher_proto;
    auto* matcher_entry = matcher_proto.mutable_matcher_list()->add_matchers();
    *matcher_entry->mutable_predicate() = match_predicate;
    envoy::extensions::filters::http::proto_api_scrubber::v3::RemoveFieldAction remove_action;
    matcher_entry->mutable_on_match()->mutable_action()->mutable_typed_config()->PackFrom(
        remove_action);
    matcher_entry->mutable_on_match()->mutable_action()->set_name("remove_field");

    *message_config.mutable_config()->mutable_matcher() = matcher_proto;

    Protobuf::Any any_config;
    any_config.PackFrom(config);
    return fmt::format(R"EOF(
    name: envoy.filters.http.proto_api_scrubber
    typed_config: {})EOF",
                       MessageUtil::getJsonStringFromMessageOrError(any_config));
  }

  template <typename T>
  IntegrationStreamDecoderPtr
  sendGrpcRequest(const T& request_msg, const std::string& method_path,
                  const Http::TestRequestHeaderMapImpl& custom_headers = {}) {
    // Close the existing connection in case it exists.
    // This can happen if this method is called more than once from a single test.
    if (codec_client_ != nullptr) {
      codec_client_->close();
    }

    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto request_buf = Grpc::Common::serializeToGrpcFrame(request_msg);

    // Default headers
    auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                          {":path", method_path},
                                                          {"content-type", "application/grpc"},
                                                          {":authority", "host"},
                                                          {":scheme", "http"}};

    // Merge custom headers (overwriting defaults if keys match)
    custom_headers.iterate(
        [&request_headers](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
          request_headers.setCopy(Http::LowerCaseString(header.key().getStringView()),
                                  header.value().getStringView());
          return Http::HeaderMap::Iterate::Continue;
        });

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
      scrubberTestDescriptorPath(),
      "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub",
      {
          "tags.value",                    // String to String map: value should be scrubbed.
          "int_map.value",                 // String to Int map: value should be scrubbed.
                                           // String to Int map: value kept (Implicit).
          "deep_map.value.secret",         // String to Object map: partial field scrubbed.
          "object_map.value.secret",       // String to Object map: all fields scrubbed (A).
          "object_map.value.public_field", // String to Object map: all fields scrubbed (B).
          "object_map.value.other_info",   // String to Object map: all fields scrubbed (C).
          "full_scrub_map.value",          // String to Object map: object itself scrubbed.
          "deep_map.value.internal_details.deep_secret" // 2-Level Deep Nesting in Map.
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

  // 2-Level Deep Nesting (Map Value -> Message -> Message -> Field).
  // deep_map.value.internal_details.deep_secret is scrubbed.
  // deep_map.value.internal_details.deep_public is kept.
  auto& deep_nested = (*request.mutable_deep_map())["k_deep_nested"];
  deep_nested.mutable_internal_details()->set_deep_secret("secret_cvv");
  deep_nested.mutable_internal_details()->set_deep_public("public_name");

  auto response = sendGrpcRequest(
      request, "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub");
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

  // 2-Level Deep Nesting Verification.
  auto& deep_nested_exp = (*expected.mutable_deep_map())["k_deep_nested"];
  deep_nested_exp.mutable_internal_details()->set_deep_secret(""); // Scrubbed
  // deep_public remains "public_name"

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<scrubber_test::ScrubRequest>(data, {expected});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// Tests that fields inside a google.protobuf.Any are scrubbed using message-level restrictions.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubAnyField_MessageLevel) {
  std::string sensitive_type = "test.extensions.filters.http.proto_api_scrubber.SensitiveMessage";
  // Configure to scrub "secret" field whenever "SensitiveMessage" is encountered.
  config_helper_.prependFilter(
      getMessageLevelFilterConfig(scrubberTestDescriptorPath(), sensitive_type, "secret"));

  initialize();

  // Create request with Any field packed with SensitiveMessage
  scrubber_test::ScrubRequest request;
  scrubber_test::SensitiveMessage sensitive;
  sensitive.set_secret("my_secret");
  sensitive.set_public_field("public_data");
  request.mutable_any_field()->PackFrom(sensitive);

  auto response = sendGrpcRequest(
      request, "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub");
  waitForNextUpstreamRequest();

  // Construct expected request
  scrubber_test::ScrubRequest expected_request = request;
  scrubber_test::SensitiveMessage expected_sensitive;
  expected_sensitive.set_public_field("public_data");
  // "secret" is NOT set (scrubbed)
  expected_request.mutable_any_field()->PackFrom(expected_sensitive);

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<scrubber_test::ScrubRequest>(data, {expected_request});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// Tests nested/recursive Any scrubbing.
// Structure: Any -> SensitiveMessage (outer) -> Nested Any -> SensitiveMessage (inner).
// Rule: Scrub 'secret' from SensitiveMessage.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubNestedAny_DeepRecursion) {
  std::string sensitive_type = "test.extensions.filters.http.proto_api_scrubber.SensitiveMessage";
  // Configure to scrub "secret" field whenever "SensitiveMessage" is encountered.
  config_helper_.prependFilter(
      getMessageLevelFilterConfig(scrubberTestDescriptorPath(), sensitive_type, "secret"));

  initialize();

  // Create Inner SensitiveMessage.
  scrubber_test::SensitiveMessage inner_sensitive;
  inner_sensitive.set_secret("inner_secret"); // Should be scrubbed.
  inner_sensitive.set_public_field("inner_public");

  // Create Outer SensitiveMessage containing Inner in 'nested_any'.
  scrubber_test::SensitiveMessage outer_sensitive;
  outer_sensitive.set_secret("outer_secret"); // Should be scrubbed.
  outer_sensitive.set_public_field("outer_public");
  outer_sensitive.mutable_nested_any()->PackFrom(inner_sensitive);

  // Create Request containing Outer in 'any_field'.
  scrubber_test::ScrubRequest request;
  request.mutable_any_field()->PackFrom(outer_sensitive);

  auto response = sendGrpcRequest(
      request, "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub");
  waitForNextUpstreamRequest();

  // Construct Expected Output.
  scrubber_test::SensitiveMessage expected_inner;
  expected_inner.set_public_field("inner_public");
  // secret cleared.

  scrubber_test::SensitiveMessage expected_outer;
  expected_outer.set_public_field("outer_public");
  // secret cleared.
  expected_outer.mutable_nested_any()->PackFrom(expected_inner);

  scrubber_test::ScrubRequest expected_request;
  expected_request.mutable_any_field()->PackFrom(expected_outer);

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<scrubber_test::ScrubRequest>(data, {expected_request});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// Tests path collision: Top level field (scrubbed) vs Nested field in Any (kept).
// Both have field name "duplicate_field".
// Method Restriction: Scrub "duplicate_field" (This applies to the top-level field).
// Message Restriction: Keep "duplicate_field" in SensitiveMessage (inside Any).
TEST_P(ProtoApiScrubberIntegrationTest, ScrubPathCollision_TopLevelVsAny) {
  ProtoApiScrubberConfig config;
  config.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);
  config.mutable_descriptor_set()->mutable_data_source()->set_filename(
      scrubberTestDescriptorPath());

  std::string method_name =
      "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub";

  // Method Restriction: Scrub "duplicate_field".
  // This rule matches the path "duplicate_field" from the root.
  // When inside Any, the path resets to "duplicate_field" as well.
  // So we expect this to potentially match BOTH if we don't have a specific message rule override.
  auto* method_restrictions = config.mutable_restrictions()->mutable_method_restrictions();
  auto& method_config = (*method_restrictions)[method_name];
  auto* req_map = method_config.mutable_request_field_restrictions();

  // Matcher for TRUE (Scrub).
  xds::type::matcher::v3::Matcher scrub_matcher;
  {
    auto* entry = scrub_matcher.mutable_matcher_list()->add_matchers();
    *entry->mutable_predicate() = buildCelPredicate("true");
    envoy::extensions::filters::http::proto_api_scrubber::v3::RemoveFieldAction remove_action;
    entry->mutable_on_match()->mutable_action()->mutable_typed_config()->PackFrom(remove_action);
    entry->mutable_on_match()->mutable_action()->set_name("remove_field");
  }
  *(*req_map)["duplicate_field"].mutable_matcher() = scrub_matcher;

  // Message Restriction: Keep "duplicate_field" in SensitiveMessage.
  // We define a rule that does NOT return RemoveFieldAction.
  // A matcher with predicate "false" returns no action, which implies "Keep".
  std::string sensitive_type = "test.extensions.filters.http.proto_api_scrubber.SensitiveMessage";
  auto* message_restrictions = config.mutable_restrictions()->mutable_message_restrictions();
  auto& message_config = (*message_restrictions)[sensitive_type];

  xds::type::matcher::v3::Matcher keep_matcher;
  {
    auto* entry = keep_matcher.mutable_matcher_list()->add_matchers();
    *entry->mutable_predicate() = buildCelPredicate("false"); // Never matches -> No action -> Keep
    envoy::extensions::filters::http::proto_api_scrubber::v3::RemoveFieldAction remove_action;
    entry->mutable_on_match()->mutable_action()->mutable_typed_config()->PackFrom(remove_action);
    entry->mutable_on_match()->mutable_action()->set_name("keep_field");
  }
  *(*message_config.mutable_field_restrictions())["duplicate_field"].mutable_matcher() =
      keep_matcher;

  Protobuf::Any any_config;
  any_config.PackFrom(config);
  std::string yaml_config = fmt::format(R"EOF(
    name: envoy.filters.http.proto_api_scrubber
    typed_config: {})EOF",
                                        MessageUtil::getJsonStringFromMessageOrError(any_config));

  config_helper_.prependFilter(yaml_config);

  initialize();

  // Construct Request.
  scrubber_test::ScrubRequest request;
  request.set_duplicate_field("top_level_value"); // Should be scrubbed.

  scrubber_test::SensitiveMessage sensitive;
  sensitive.set_duplicate_field("nested_value"); // Should be kept.
  request.mutable_any_field()->PackFrom(sensitive);

  auto response = sendGrpcRequest(request, method_name);
  waitForNextUpstreamRequest();

  // Verification.
  scrubber_test::ScrubRequest expected_request = request;
  expected_request.set_duplicate_field(""); // Top level scrubbed.

  // Nested kept (SensitiveMessage.duplicate_field remains "nested_value").
  // (Note: `expected_request` copied `request`, so it already has the populated Any field).

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<scrubber_test::ScrubRequest>(data, {expected_request});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// Tests recursive scrubbing of messages.
// Recursive path: root -> recursive_child -> recursive_child.
// Rule: Scrub 'secret' from SensitiveMessage.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubRecursiveMessage) {
  std::string sensitive_type = "test.extensions.filters.http.proto_api_scrubber.SensitiveMessage";
  // Rule: Scrub 'secret' in 'SensitiveMessage'.
  config_helper_.prependFilter(
      getMessageLevelFilterConfig(scrubberTestDescriptorPath(), sensitive_type, "secret"));

  initialize();

  scrubber_test::ScrubRequest request;

  // Level 1.
  auto& l1 = (*request.mutable_deep_map())["root"];
  l1.set_secret("secret_1");
  l1.set_public_field("public_1");

  // Level 2.
  auto* l2 = l1.mutable_recursive_child();
  l2->set_secret("secret_2");
  l2->set_public_field("public_2");

  // Level 3.
  auto* l3 = l2->mutable_recursive_child();
  l3->set_secret("secret_3");
  l3->set_public_field("public_3");

  auto response = sendGrpcRequest(
      request, "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub");
  waitForNextUpstreamRequest();

  // Verification.
  scrubber_test::ScrubRequest expected = request;

  // L1 Scrubbed.
  (*expected.mutable_deep_map())["root"].set_secret("");

  // L2 Scrubbed.
  (*expected.mutable_deep_map())["root"].mutable_recursive_child()->set_secret("");

  // L3 Scrubbed.
  (*expected.mutable_deep_map())["root"]
      .mutable_recursive_child()
      ->mutable_recursive_child()
      ->set_secret("");

  // Check.
  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<scrubber_test::ScrubRequest>(data, {expected});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// Tests that a message type is scrubbed globally (standard field).
// Rule: Scrub 'SensitiveMessage' everywhere.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubGlobalMessageType) {
  std::string sensitive_type = "test.extensions.filters.http.proto_api_scrubber.SensitiveMessage";
  config_helper_.prependFilter(
      getGlobalTypeFilterConfig(scrubberTestDescriptorPath(), sensitive_type));

  initialize();

  scrubber_test::ScrubRequest request;
  // Field 'object_map' contains SensitiveMessage as value.
  auto& sensitive = (*request.mutable_object_map())["key"];
  sensitive.set_secret("data");

  auto response = sendGrpcRequest(
      request, "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub");
  waitForNextUpstreamRequest();

  // The field 'object_map["key"]' is of type SensitiveMessage.
  // Since the type is restricted globally, the field itself should be removed.
  scrubber_test::ScrubRequest expected = request;
  expected.mutable_object_map()->erase("key");

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<scrubber_test::ScrubRequest>(data, {expected});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// Tests that a message type is scrubbed globally when inside Any.
// Rule: Scrub 'SensitiveMessage' everywhere.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubGlobalMessageType_InsideAny) {
  std::string sensitive_type = "test.extensions.filters.http.proto_api_scrubber.SensitiveMessage";
  config_helper_.prependFilter(
      getGlobalTypeFilterConfig(scrubberTestDescriptorPath(), sensitive_type));

  initialize();

  scrubber_test::ScrubRequest request;
  scrubber_test::SensitiveMessage sensitive;
  sensitive.set_secret("data");
  request.mutable_any_field()->PackFrom(sensitive);

  auto response = sendGrpcRequest(
      request, "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub");
  waitForNextUpstreamRequest();

  // The Any field contains a restricted type. The filter should completely remove the Any field
  // content. (Implementation detail: The ProtoScrubber usually clears the field if CheckType
  // returns Exclude).
  scrubber_test::ScrubRequest expected = request;
  expected.clear_any_field();

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<scrubber_test::ScrubRequest>(data, {expected});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// Re-implementing a simple Message Type Global Scrub for Integration consistency
// since Enum integration relies on proto changes.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubGlobalMessageType_NestedDeep) {
  std::string inner_type = "test.extensions.filters.http.proto_api_scrubber.InnerDetails";
  config_helper_.prependFilter(getGlobalTypeFilterConfig(scrubberTestDescriptorPath(), inner_type));

  initialize();

  scrubber_test::ScrubRequest request;
  auto& deep = (*request.mutable_deep_map())["k"];
  // InnerDetails is field 'internal_details' inside SensitiveMessage.
  deep.mutable_internal_details()->set_deep_secret("secret");

  auto response = sendGrpcRequest(
      request, "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub");
  waitForNextUpstreamRequest();

  // The 'internal_details' field is of type InnerDetails.
  // It should be removed.
  scrubber_test::ScrubRequest expected = request;
  (*expected.mutable_deep_map())["k"].clear_internal_details();

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<scrubber_test::ScrubRequest>(data, {expected});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// ============================================================================
// TEST GROUP 1: PASS THROUGH & METRICS
// ============================================================================

// Tests that the simple non-streaming request passes through without modification if there are no
// restrictions configured in the filter config. Also verifies stats.
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

  // Verify Stats
  test_server_->waitForCounterGe("proto_api_scrubber.total_requests_checked", 1);
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

// Tests scrubbing of nested fields in the request when a CEL matcher is configured to use request
// headers.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubNestedField_CustomCelMatcher_RequestHeader) {
  config_helper_.prependFilter(getFilterConfig(
      apikeysDescriptorPath(), kCreateApiKeyMethod, "key.display_name", RestrictionType::Request,
      buildCelPredicate("request.headers['api-version'] == '2025-v1'")));
  initialize();

  {
    // Tests that the field `key.display_name` is removed from the request as the CEL expression
    // ("request.headers['api-version'] == '2025_v1'") evaluates to true.
    auto original_proto = makeCreateApiKeyRequest(R"pb(
      parent: "public"
      key { display_name: "should-be-removed" }
    )pb");
    auto custom_headers = Http::TestRequestHeaderMapImpl{{"api-version", "2025-v1"}};

    auto response = sendGrpcRequest(original_proto, kCreateApiKeyMethod, custom_headers);
    waitForNextUpstreamRequest();

    apikeys::CreateApiKeyRequest expected = original_proto;
    expected.mutable_key()->clear_display_name();

    Buffer::OwnedImpl data;
    data.add(upstream_request_->body());
    checkSerializedData<apikeys::CreateApiKeyRequest>(data, {expected});

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
  }

  {
    // Tests that the field `key.display_name` is preserved in the request as the CEL expression
    // ("request.headers['api-version'] == '2025_v1'") evaluates to false.
    auto original_proto = makeCreateApiKeyRequest(R"pb(
      parent: "public"
      key { display_name: "should-stay" }
    )pb");
    auto custom_headers = Http::TestRequestHeaderMapImpl{{"api-version", "2025-v2"}};

    auto response = sendGrpcRequest(original_proto, kCreateApiKeyMethod, custom_headers);
    waitForNextUpstreamRequest();

    Buffer::OwnedImpl data;
    data.add(upstream_request_->body());
    checkSerializedData<apikeys::CreateApiKeyRequest>(data, {original_proto});

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
  }
}

// Tests scrubbing of nested fields in the request when a String matcher is configured to use
// request headers via exact match.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubNestedField_StringMatcher_RequestHeader_ExactMatch) {
  envoy::type::matcher::v3::HttpRequestHeaderMatchInput header_input;
  header_input.set_header_name("api-version");

  auto predicate = buildStringMatcherPredicate("envoy.matching.inputs.request_headers",
                                               header_input, "2025-v1", StringMatchType::Exact);

  config_helper_.prependFilter(getFilterConfig(apikeysDescriptorPath(), kCreateApiKeyMethod,
                                               "key.display_name", RestrictionType::Request,
                                               predicate));

  initialize();

  auto original_proto = makeCreateApiKeyRequest(R"pb(
      parent: "public"
      key { display_name: "should-be-removed" }
    )pb");
  auto custom_headers = Http::TestRequestHeaderMapImpl{{"api-version", "2025-v1"}};

  auto response = sendGrpcRequest(original_proto, kCreateApiKeyMethod, custom_headers);
  waitForNextUpstreamRequest();

  apikeys::CreateApiKeyRequest expected = original_proto;
  expected.mutable_key()->clear_display_name();

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<apikeys::CreateApiKeyRequest>(data, {expected});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// Tests scrubbing of nested fields in the request when a String matcher is configured to use filter
// state via regex match.
TEST_P(ProtoApiScrubberIntegrationTest, ScrubNestedField_StringMatcher_FilterState_RegexMatch) {
  envoy::extensions::matching::common_inputs::network::v3::FilterStateInput filter_state_input;
  filter_state_input.set_key(kFilterStateLabelKey);

  // The metadata injector filter (test_injector) sets the value to: "LABEL1,LABEL2,LABEL3"
  // We use a regex to verify that "LABEL2" is present in the list.
  auto predicate =
      buildStringMatcherPredicate("envoy.matching.inputs.filter_state", filter_state_input,
                                  ".*LABEL2.*", StringMatchType::Regex);

  config_helper_.prependFilter(getFilterConfig(apikeysDescriptorPath(), kCreateApiKeyMethod,
                                               "key.display_name", RestrictionType::Request,
                                               predicate));

  config_helper_.prependFilter(R"yaml(
    name: test_injector
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
  )yaml");

  initialize();

  auto original_proto = makeCreateApiKeyRequest(R"pb(
    parent: "public"
    key { display_name: "should-be-removed" }
  )pb");

  auto response = sendGrpcRequest(original_proto, kCreateApiKeyMethod);
  waitForNextUpstreamRequest();

  // Since "LABEL1,LABEL2,LABEL3" matches ".*LABEL2.*", the field should be removed.
  apikeys::CreateApiKeyRequest expected = original_proto;
  expected.mutable_key()->clear_display_name();

  Buffer::OwnedImpl data;
  data.add(upstream_request_->body());
  checkSerializedData<apikeys::CreateApiKeyRequest>(data, {expected});

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// ============================================================================
// TEST GROUP 3: VALIDATION, REJECTION & METRICS
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

  // Verify Stats: Scrubbing failed because method not found implies no type info found.
  test_server_->waitForCounterGe("proto_api_scrubber.request_scrubbing_failed", 1);
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

  // Verify Stats for invalid method name.
  test_server_->waitForCounterGe("proto_api_scrubber.invalid_method_name", 1);
}

// Tests that the request is rejected if a method-level block rule matches.
TEST_P(ProtoApiScrubberIntegrationTest, RejectsBlockedMethod) {
  // Construct the config programmatically.
  ProtoApiScrubberConfig config;
  config.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);
  config.mutable_descriptor_set()->mutable_data_source()->set_filename(apikeysDescriptorPath());

  // Create the CEL matcher for "true" (always match -> block).
  auto matcher_predicate = buildCelPredicate("true");

  // Create the full Matcher object.
  xds::type::matcher::v3::Matcher matcher;
  auto* matcher_entry = matcher.mutable_matcher_list()->add_matchers();
  *matcher_entry->mutable_predicate() = matcher_predicate;

  // Use RemoveFieldAction as a placeholder action since the logic only checks for a match.
  envoy::extensions::filters::http::proto_api_scrubber::v3::RemoveFieldAction remove_action;
  matcher_entry->mutable_on_match()->mutable_action()->mutable_typed_config()->PackFrom(
      remove_action);
  matcher_entry->mutable_on_match()->mutable_action()->set_name("block_action");

  // Set up the restriction map.
  auto& method_restrictions = *config.mutable_restrictions()->mutable_method_restrictions();
  auto& restriction = method_restrictions[kCreateApiKeyMethod];
  *restriction.mutable_method_restriction()->mutable_matcher() = matcher;

  // Wrap in Any and convert to JSON for Envoy config.
  Protobuf::Any any_config;
  any_config.PackFrom(config);
  std::string typed_config = fmt::format(R"EOF(
    name: envoy.filters.http.proto_api_scrubber
    typed_config: {})EOF",
                                         MessageUtil::getJsonStringFromMessageOrError(any_config));

  config_helper_.prependFilter(typed_config);
  initialize();

  auto request_proto = makeCreateApiKeyRequest();
  auto response = sendGrpcRequest(request_proto, kCreateApiKeyMethod);
  ASSERT_TRUE(response->waitForEndStream());

  // Verify 404 / Not Found.
  auto grpc_status = response->headers().GrpcStatus();
  ASSERT_TRUE(grpc_status != nullptr);
  EXPECT_EQ("5", grpc_status->value().getStringView()); // 5 = Not Found.

  // Verify Stats for blocked method.
  test_server_->waitForCounterGe("proto_api_scrubber.method_blocked", 1);
}

} // namespace
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
