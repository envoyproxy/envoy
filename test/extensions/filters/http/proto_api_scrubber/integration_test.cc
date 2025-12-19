#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib/field_checker.h"

#include "test/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_test_lib.h"
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

using envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig;
using envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter;
using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::checkSerializedData;

std::string apikeysDescriptorPath() {
  return TestEnvironment::runfilesPath("test/proto/apikeys.descriptor");
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

  // Helper to build the configuration with a generic predicate.
  std::string
  getFilterConfig(const std::string& descriptor_path, const std::string& method_name = "",
                  const std::string& field_to_scrub = "",
                  RestrictionType type = RestrictionType::Request,
                  const xds::type::matcher::v3::Matcher::MatcherList::Predicate& match_predicate =
                      buildCelPredicate("true")) {
    std::string full_config_text;
    if (method_name.empty() || field_to_scrub.empty()) {
      full_config_text = fmt::format(R"pb(
      filtering_mode: OVERRIDE
      descriptor_set {{ data_source {{ filename: "{0}" }} }}
    )pb",
                                     descriptor_path);
    } else {
      std::string restriction_key = (type == RestrictionType::Request)
                                        ? "request_field_restrictions"
                                        : "response_field_restrictions";

      // Build the Matcher
      xds::type::matcher::v3::Matcher matcher_proto;
      auto* matcher_entry = matcher_proto.mutable_matcher_list()->add_matchers();
      *matcher_entry->mutable_predicate() = match_predicate;
      envoy::extensions::filters::http::proto_api_scrubber::v3::RemoveFieldAction remove_action;
      matcher_entry->mutable_on_match()->mutable_action()->mutable_typed_config()->PackFrom(
          remove_action);
      matcher_entry->mutable_on_match()->mutable_action()->set_name("remove_field");

      std::string matcher_text;
      Protobuf::TextFormat::PrintToString(matcher_proto, &matcher_text);
      full_config_text = fmt::format(R"pb(
      filtering_mode: OVERRIDE
      descriptor_set {{
        data_source {{ filename: "{0}" }}
      }}
      restrictions {{
        method_restrictions {{
          key: "{1}"
          value {{
            {2} {{
              key: "{3}"
              value {{
                matcher {{ {4} }}
              }}
            }}
          }}
        }}
      }}
    )pb",
                                     descriptor_path, // {0}
                                     method_name,     // {1}
                                     restriction_key, // {2}
                                     field_to_scrub,  // {3}
                                     matcher_text     // {4} Inject the Generic Matcher
      );
    }

    ProtoApiScrubberConfig filter_config_proto;
    Protobuf::TextFormat::ParseFromString(full_config_text, &filter_config_proto);

    Protobuf::Any any_config;
    any_config.PackFrom(filter_config_proto);
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
