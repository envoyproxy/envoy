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

TEST(HeaderMutationFilterTest, HeaderMutationFilterTest) {
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
    request_mutations:
    - append:
        header:
          key: "global-flag-header"
          value: "global-flag-header-value"
        append_action: "ADD_IF_ABSENT"
    response_mutations:
    - remove: "global-flag-header"
  )EOF";

  PerRouteProtoConfig per_route_proto_config;
  TestUtility::loadFromYaml(route_config_yaml, per_route_proto_config);

  PerRouteHeaderMutationSharedPtr config =
      std::make_shared<PerRouteHeaderMutation>(per_route_proto_config);

  ProtoConfig proto_config;
  TestUtility::loadFromYaml(config_yaml, proto_config);
  HeaderMutationConfigSharedPtr global_config =
      std::make_shared<HeaderMutationConfig>(proto_config);

  {
    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

    HeaderMutation filter{global_config};
    filter.setDecoderFilterCallbacks(decoder_callbacks);
    filter.setEncoderFilterCallbacks(encoder_callbacks);

    ON_CALL(decoder_callbacks, mostSpecificPerFilterConfig())
        .WillByDefault(testing::Return(config.get()));
    ON_CALL(decoder_callbacks, mostSpecificPerFilterConfig())
        .WillByDefault(testing::Return(config.get()));

    {
      Envoy::Http::TestRequestHeaderMapImpl headers = {
          {"flag-header", "flag-header-value"},
          {"another-flag-header", "another-flag-header-value"},
          {"flag-header-2", "flag-header-2-value-old"},
          {"flag-header-3", "flag-header-3-value-old"},
          {"flag-header-4", "flag-header-4-value-old"},
          {"flag-header-6", "flag-header-6-value-old"},
          {":method", "GET"},
          {":path", "/"},
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
    }

    // Case where the decodeHeaders() is not called and the encodeHeaders() is called.
    {
      Envoy::Http::TestResponseHeaderMapImpl headers = {
          {"flag-header", "flag-header-value"},
          {"another-flag-header", "another-flag-header-value"},
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
    }

    // Case where the request headers map is nullptr.
    {
      Envoy::Http::TestResponseHeaderMapImpl headers = {
          {"flag-header", "flag-header-value"},
          {"another-flag-header", "another-flag-header-value"},
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
    }
  }

  {
    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

    HeaderMutation filter{global_config};
    filter.setDecoderFilterCallbacks(decoder_callbacks);
    filter.setEncoderFilterCallbacks(encoder_callbacks);

    Envoy::Http::TestResponseHeaderMapImpl headers = {
        {"flag-header", "flag-header-value"},
        {"another-flag-header", "another-flag-header-value"},
        {"flag-header-2", "flag-header-2-value-old"},
        {"flag-header-3", "flag-header-3-value-old"},
        {"flag-header-4", "flag-header-4-value-old"},
        {"flag-header-6", "flag-header-6-value-old"},
        {":status", "200"},
    };

    // If the decoding phase is not performed then try to get the config from the encoding phase.
    EXPECT_CALL(encoder_callbacks, mostSpecificPerFilterConfig())
        .WillRepeatedly(testing::Return(config.get()));

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
  }

  {
    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

    HeaderMutation filter{global_config};
    filter.setDecoderFilterCallbacks(decoder_callbacks);
    filter.setEncoderFilterCallbacks(encoder_callbacks);

    Envoy::Http::TestRequestHeaderMapImpl request_headers = {
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};

    Envoy::Http::TestResponseHeaderMapImpl response_headers = {
        {"global-flag-header", "global-flag-header-value"},
        {":status", "200"},
    };

    EXPECT_CALL(decoder_callbacks, mostSpecificPerFilterConfig())
        .WillRepeatedly(testing::Return(nullptr));
    EXPECT_CALL(encoder_callbacks, mostSpecificPerFilterConfig())
        .WillRepeatedly(testing::Return(nullptr));

    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.decodeHeaders(request_headers, true));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.encodeHeaders(response_headers, true));

    EXPECT_EQ("global-flag-header-value", request_headers.get_("global-flag-header"));
    EXPECT_EQ("", response_headers.get_("global-flag-header"));
  }
}

} // namespace
} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
