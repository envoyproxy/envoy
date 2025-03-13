#include "source/extensions/filters/http/header_mutation/header_mutation.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {
namespace {

using testing::NiceMock;

TEST(HeaderMutationFilterTest, RequestMutationTest) {
  const std::string route_config_yaml = R"EOF(
  mutations:
    request_mutations:
    - remove: "flag-header"
    - append:
        header:
          key: "flag-header"
          value: "%REQ(ANOTHER-FLAG-HEADER)%"
        append_action: "APPEND_IF_EXISTS_OR_ADD"
    - append:
        header:
          key: "flag-header-2"
          value: "flag-header-2-value"
        append_action: "APPEND_IF_EXISTS_OR_ADD"
    - append:
        header:
          key: "flag-header-3"
          value: "flag-header-3-value"
        append_action: "ADD_IF_ABSENT"
    - append:
        header:
          key: "flag-header-4"
          value: "flag-header-4-value"
        append_action: "OVERWRITE_IF_EXISTS_OR_ADD"
    - append:
        header:
          key: "flag-header-5"
          value: "flag-header-5-value"
        append_action: "OVERWRITE_IF_EXISTS"
    - append:
        header:
          key: "flag-header-6"
          value: "flag-header-6-value"
        append_action: "OVERWRITE_IF_EXISTS"
  )EOF";

  const std::string config_yaml = R"EOF(
  mutations:
    request_mutations:
    - append:
        header:
          key: "global-flag-header"
          value: "global-flag-header-value"
        append_action: "ADD_IF_ABSENT"
  )EOF";

  PerRouteProtoConfig per_route_proto_config;
  TestUtility::loadFromYaml(route_config_yaml, per_route_proto_config);

  absl::Status creation_status = absl::OkStatus();
  PerRouteHeaderMutationSharedPtr config =
      std::make_shared<PerRouteHeaderMutation>(per_route_proto_config, creation_status);

  ProtoConfig proto_config;
  TestUtility::loadFromYaml(config_yaml, proto_config);
  HeaderMutationConfigSharedPtr global_config =
      std::make_shared<HeaderMutationConfig>(proto_config, creation_status);

  {
    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

    HeaderMutation filter{global_config};
    filter.setDecoderFilterCallbacks(decoder_callbacks);
    filter.setEncoderFilterCallbacks(encoder_callbacks);

    EXPECT_CALL(*decoder_callbacks.route_, perFilterConfigs(_))
        .WillOnce(Invoke([&](absl::string_view) -> Router::RouteSpecificFilterConfigs {
          return {config.get()};
        }));

    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {"flag-header", "flag-header-value"},
        {"another-flag-header", "another-flag-header-value"},
        {"flag-header-2", "flag-header-2-value-old"},
        {"flag-header-3", "flag-header-3-value-old"},
        {"flag-header-4", "flag-header-4-value-old"},
        {"flag-header-6", "flag-header-6-value-old"},
        {":method", "GET"},
        {":path", "/path"},
        {":scheme", "http"},
        {":authority", "host"}};

    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.decodeHeaders(headers, true));

    // 'flag-header' is removed and new 'flag-header' is added.
    EXPECT_EQ("another-flag-header-value", headers.get_("flag-header"));
    // 'flag-header-2' is appended.
    EXPECT_EQ(2, headers.get(Envoy::Http::LowerCaseString("flag-header-2")).size());
    // 'flag-header-3' is not appended and keep the old value.
    EXPECT_EQ(1, headers.get(Envoy::Http::LowerCaseString("flag-header-3")).size());
    EXPECT_EQ("flag-header-3-value-old", headers.get_("flag-header-3"));
    // 'flag-header-4' is overwritten.
    EXPECT_EQ(1, headers.get(Envoy::Http::LowerCaseString("flag-header-4")).size());
    EXPECT_EQ("flag-header-4-value", headers.get_("flag-header-4"));
    // 'flag-header-5' was not present, so will not be present after mutation.
    EXPECT_FALSE(headers.has("flag-header-5"));
    // 'flag-header-6' was present and should be overwritten.
    EXPECT_EQ("flag-header-6-value", headers.get_("flag-header-6"));
    // global header is added.
    EXPECT_EQ("global-flag-header-value", headers.get_("global-flag-header"));
  }
}

TEST(HeaderMutationFilterTest, ResponseMutationTest) {
  const std::string route_config_yaml = R"EOF(
  mutations:
    response_mutations:
    - remove: "flag-header"
    - append:
        header:
          key: "flag-header"
          value: "%RESP(ANOTHER-FLAG-HEADER)%"
        append_action: "APPEND_IF_EXISTS_OR_ADD"
    - append:
        header:
          key: "flag-header-2"
          value: "flag-header-2-value"
        append_action: "APPEND_IF_EXISTS_OR_ADD"
    - append:
        header:
          key: "flag-header-3"
          value: "flag-header-3-value"
        append_action: "ADD_IF_ABSENT"
    - append:
        header:
          key: "flag-header-4"
          value: "flag-header-4-value"
        append_action: "OVERWRITE_IF_EXISTS_OR_ADD"
    - append:
        header:
          key: "flag-header-5"
          value: "flag-header-5-value"
        append_action: "OVERWRITE_IF_EXISTS"
    - append:
        header:
          key: "flag-header-6"
          value: "flag-header-6-value"
        append_action: "OVERWRITE_IF_EXISTS"
  )EOF";

  const std::string config_yaml = R"EOF(
  mutations:
    response_mutations:
    - remove: "global-flag-header"
  )EOF";

  PerRouteProtoConfig per_route_proto_config;
  TestUtility::loadFromYaml(route_config_yaml, per_route_proto_config);

  absl::Status creation_status = absl::OkStatus();
  PerRouteHeaderMutationSharedPtr config =
      std::make_shared<PerRouteHeaderMutation>(per_route_proto_config, creation_status);

  ProtoConfig proto_config;
  TestUtility::loadFromYaml(config_yaml, proto_config);
  HeaderMutationConfigSharedPtr global_config =
      std::make_shared<HeaderMutationConfig>(proto_config, creation_status);

  // Case where the decodeHeaders() is not called and the encodeHeaders() is called.
  {
    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

    HeaderMutation filter{global_config};
    filter.setDecoderFilterCallbacks(decoder_callbacks);
    filter.setEncoderFilterCallbacks(encoder_callbacks);

    EXPECT_CALL(*encoder_callbacks.route_, perFilterConfigs(_))
        .WillOnce(Invoke([&](absl::string_view) -> Router::RouteSpecificFilterConfigs {
          return {config.get()};
        }));

    Envoy::Http::TestResponseHeaderMapImpl headers = {
        {"flag-header", "flag-header-value"},
        {"another-flag-header", "another-flag-header-value"},
        {"global-flag-header", "global-flag-header-value"},
        {"flag-header-2", "flag-header-2-value-old"},
        {"flag-header-3", "flag-header-3-value-old"},
        {"flag-header-4", "flag-header-4-value-old"},
        {"flag-header-6", "flag-header-6-value-old"},
        {":status", "200"},
    };

    Http::RequestHeaderMap* request_headers_pointer =
        Http::StaticEmptyHeaders::get().request_headers.get();
    EXPECT_CALL(encoder_callbacks, requestHeaders())
        .WillOnce(testing::Return(makeOptRefFromPtr(request_headers_pointer)));

    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.encodeHeaders(headers, true));

    // 'flag-header' is removed and new 'flag-header' is added.
    EXPECT_EQ("another-flag-header-value", headers.get_("flag-header"));
    // 'flag-header-2' is appended.
    EXPECT_EQ(2, headers.get(Envoy::Http::LowerCaseString("flag-header-2")).size());
    // 'flag-header-3' is not appended and keep the old value.
    EXPECT_EQ(1, headers.get(Envoy::Http::LowerCaseString("flag-header-3")).size());
    EXPECT_EQ("flag-header-3-value-old", headers.get_("flag-header-3"));
    // 'flag-header-4' is overwritten.
    EXPECT_EQ(1, headers.get(Envoy::Http::LowerCaseString("flag-header-4")).size());
    EXPECT_EQ("flag-header-4-value", headers.get_("flag-header-4"));
    // 'flag-header-5' was not present, so will not be present after mutation.
    EXPECT_FALSE(headers.has("flag-header-5"));
    // 'flag-header-6' was present and should be overwritten.
    EXPECT_EQ("flag-header-6-value", headers.get_("flag-header-6"));
    // global header is removed.
    EXPECT_FALSE(headers.has("global-flag-header"));
  }

  // Case where the decodeHeaders() is not called and the encodeHeaders() is called and
  // the request header map is nullptr.
  {
    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

    HeaderMutation filter{global_config};
    filter.setDecoderFilterCallbacks(decoder_callbacks);
    filter.setEncoderFilterCallbacks(encoder_callbacks);

    EXPECT_CALL(*encoder_callbacks.route_, perFilterConfigs(_))
        .WillOnce(Invoke([&](absl::string_view) -> Router::RouteSpecificFilterConfigs {
          return {config.get()};
        }));

    Envoy::Http::TestResponseHeaderMapImpl headers = {
        {"flag-header", "flag-header-value"},
        {"another-flag-header", "another-flag-header-value"},
        {"global-flag-header", "global-flag-header-value"},
        {"flag-header-2", "flag-header-2-value-old"},
        {"flag-header-3", "flag-header-3-value-old"},
        {"flag-header-4", "flag-header-4-value-old"},
        {"flag-header-6", "flag-header-6-value-old"},
        {":status", "200"},
    };

    EXPECT_CALL(encoder_callbacks, requestHeaders())
        .WillOnce(testing::Return(Http::RequestHeaderMapOptRef{}));

    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.encodeHeaders(headers, true));

    // 'flag-header' is removed and new 'flag-header' is added.
    EXPECT_EQ("another-flag-header-value", headers.get_("flag-header"));
    // 'flag-header-2' is appended.
    EXPECT_EQ(2, headers.get(Envoy::Http::LowerCaseString("flag-header-2")).size());
    // 'flag-header-3' is not appended and keep the old value.
    EXPECT_EQ(1, headers.get(Envoy::Http::LowerCaseString("flag-header-3")).size());
    EXPECT_EQ("flag-header-3-value-old", headers.get_("flag-header-3"));
    // 'flag-header-4' is overwritten.
    EXPECT_EQ(1, headers.get(Envoy::Http::LowerCaseString("flag-header-4")).size());
    EXPECT_EQ("flag-header-4-value", headers.get_("flag-header-4"));
    // 'flag-header-5' was not present, so will not be present after mutation.
    EXPECT_FALSE(headers.has("flag-header-5"));
    // 'flag-header-6' was present and should be overwritten.
    EXPECT_EQ("flag-header-6-value", headers.get_("flag-header-6"));
    // global header is removed.
    EXPECT_FALSE(headers.has("global-flag-header"));
  }
}

TEST(HeaderMutationFilterTest, HybridMutationTest) {
  const std::string route_config_yaml = R"EOF(
  mutations:
    request_mutations:
    - remove: "flag-header"
    - append:
        header:
          key: "flag-header"
          value: "%REQ(ANOTHER-FLAG-HEADER)%"
        append_action: "APPEND_IF_EXISTS_OR_ADD"
    response_mutations:
    - remove: "flag-header"
    - append:
        header:
          key: "flag-header"
          value: "%RESP(ANOTHER-FLAG-HEADER)%"
        append_action: "APPEND_IF_EXISTS_OR_ADD"
    query_parameter_mutations:
    - remove: "flag-query"
    - append:
        record:
          key: "flag-query"
          value: "%REQ(ANOTHER-FLAG-QUERY)%"
        action: "APPEND_IF_EXISTS_OR_ADD"
  )EOF";

  const std::string config_yaml = R"EOF(
  mutations:
    request_mutations:
    - append:
        header:
          key: "global-flag-header"
          value: "global-flag-header-value"
    response_mutations:
    - remove: "global-flag-header"
  )EOF";

  PerRouteProtoConfig per_route_proto_config;
  TestUtility::loadFromYaml(route_config_yaml, per_route_proto_config);

  absl::Status creation_status = absl::OkStatus();
  PerRouteHeaderMutationSharedPtr config =
      std::make_shared<PerRouteHeaderMutation>(per_route_proto_config, creation_status);

  ProtoConfig proto_config;
  TestUtility::loadFromYaml(config_yaml, proto_config);
  HeaderMutationConfigSharedPtr global_config =
      std::make_shared<HeaderMutationConfig>(proto_config, creation_status);

  {
    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

    HeaderMutation filter{global_config};
    filter.setDecoderFilterCallbacks(decoder_callbacks);
    filter.setEncoderFilterCallbacks(encoder_callbacks);

    EXPECT_CALL(*decoder_callbacks.route_, perFilterConfigs(_))
        .WillOnce(Invoke([&](absl::string_view) -> Router::RouteSpecificFilterConfigs {
          return {config.get()};
        }));

    Envoy::Http::TestRequestHeaderMapImpl request_headers = {
        {"flag-header", "flag-header-value"},
        {"another-flag-header", "another-flag-header-value"},
        {"another-flag-query", "another-flag-query-value"},
        {":method", "GET"},
        {":path", "/path?flag-query=flag-query-value"},
        {":scheme", "http"},
        {":authority", "host"}};

    Envoy::Http::TestResponseHeaderMapImpl response_headers = {
        {"global-flag-header", "global-flag-header-value"},
        {"flag-header", "flag-header-value"},
        {"another-flag-header", "another-flag-header-value"},
        {":status", "200"},
    };

    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.decodeHeaders(request_headers, true));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.encodeHeaders(response_headers, true));

    // Request header mutation.
    // 'flag-header' is removed and new 'flag-header' is added.
    EXPECT_EQ("another-flag-header-value", request_headers.get_("flag-header"));
    // global header is added.
    EXPECT_EQ("global-flag-header-value", request_headers.get_("global-flag-header"));
    // 'flag-query' is removed and new 'flag-query' is added.
    auto params =
        Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(request_headers.getPathValue());
    EXPECT_EQ("another-flag-query-value", params.data().at("flag-query").front());

    // Response header mutation.
    // 'flag-header' is removed and new 'flag-header' is added.
    EXPECT_EQ("another-flag-header-value", response_headers.get_("flag-header"));
    // global header is removed.
    EXPECT_FALSE(response_headers.has("global-flag-header"));
  }
}

TEST(HeaderMutationFilterTest, QueryParameterMutationTest) {
  const std::string route_config_yaml = R"EOF(
  mutations:
    query_parameter_mutations:
    - remove: "flag-query"
    - append:
        record:
          key: "flag-query"
          value: "%REQ(ANOTHER-FLAG-QUERY)%"
        action: "APPEND_IF_EXISTS_OR_ADD"
    - append:
        record:
          key: "flag-query-2"
          value: "flag-query-2-value"
        action: "APPEND_IF_EXISTS_OR_ADD"
    - append:
        record:
          key: "flag-query-3"
          value: "flag-query-3-value"
        action: "ADD_IF_ABSENT"
    - append:
        record:
          key: "flag-query-4"
          value: "flag-query-4-value"
        action: "OVERWRITE_IF_EXISTS_OR_ADD"
    - append:
        record:
          key: "flag-query-5"
          value: "flag-query-5-value"
        action: "OVERWRITE_IF_EXISTS"
    - append:
        record:
          key: "flag-query-6"
          value: "flag-query-6-value"
        action: "OVERWRITE_IF_EXISTS"
  )EOF";

  const std::string config_yaml = R"EOF(
  mutations:
    request_mutations:
    - append:
        header:
          key: "global-flag-header"
          value: "global-flag-header-value"
        append_action: "ADD_IF_ABSENT"
    query_parameter_mutations:
    - append:
        record:
          key: "global-param-key"
          value: "global-param-value"
        action: "ADD_IF_ABSENT"
    response_mutations:
    - remove: "global-flag-header"
  )EOF";

  PerRouteProtoConfig per_route_proto_config;
  TestUtility::loadFromYaml(route_config_yaml, per_route_proto_config);

  absl::Status creation_status = absl::OkStatus();
  PerRouteHeaderMutationSharedPtr config =
      std::make_shared<PerRouteHeaderMutation>(per_route_proto_config, creation_status);

  ProtoConfig proto_config;
  TestUtility::loadFromYaml(config_yaml, proto_config);
  HeaderMutationConfigSharedPtr global_config =
      std::make_shared<HeaderMutationConfig>(proto_config, creation_status);

  {
    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

    HeaderMutation filter{global_config};
    filter.setDecoderFilterCallbacks(decoder_callbacks);
    filter.setEncoderFilterCallbacks(encoder_callbacks);

    EXPECT_CALL(*decoder_callbacks.route_, perFilterConfigs(_))
        .WillOnce(Invoke([&](absl::string_view) -> Router::RouteSpecificFilterConfigs {
          return {config.get()};
        }));

    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {"another-flag-query", "another-flag-query-value"},
        {":method", "GET"},
        {":path", "/path?"
                  "flag-query=flag-query-value&"
                  "flag-query-2=flag-query-2-value-old&"
                  "flag-query-3=flag-query-3-value-old&"
                  "flag-query-4=flag-query-4-value-old&"
                  "flag-query-6=flag-query-6-value-old"},
        {":scheme", "http"},
        {":authority", "host"}};

    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.decodeHeaders(headers, true));

    auto params =
        Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(headers.getPathValue());
    // 'flag-query' is removed and new 'flag-query' is added.
    EXPECT_EQ("another-flag-query-value", params.data().at("flag-query").front());
    // 'flag-query-2' is appended.
    EXPECT_EQ(2, params.data().at("flag-query-2").size());
    // 'flag-query-3' is not appended and keep the old value.
    EXPECT_EQ("flag-query-3-value-old", params.data().at("flag-query-3").front());
    // 'flag-query-4' is overwritten.
    EXPECT_EQ(1, params.data().at("flag-query-4").size());
    EXPECT_EQ("flag-query-4-value", params.data().at("flag-query-4").front());
    // 'flag-query-5' was not present, so will not be present after mutation.
    EXPECT_FALSE(params.data().contains("flag-query-5"));
    // 'flag-query-6' was present and should be overwritten.
    EXPECT_EQ("flag-query-6-value", params.data().at("flag-query-6").front());
    // global query parameter is added.
    EXPECT_EQ("global-param-value", params.data().at("global-param-key").front());
  }
}

} // namespace
} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
