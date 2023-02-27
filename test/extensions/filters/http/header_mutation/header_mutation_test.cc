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
  const std::string config_yaml = R"EOF(
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
  )EOF";

  PerRouteProtoConfig per_route_proto_config;
  TestUtility::loadFromYaml(config_yaml, per_route_proto_config);

  PerRouteHeaderMutationSharedPtr config =
      std::make_shared<PerRouteHeaderMutation>(per_route_proto_config);

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

  {
    HeaderMutation filter{};
    filter.setDecoderFilterCallbacks(decoder_callbacks);
    filter.setEncoderFilterCallbacks(encoder_callbacks);

    {
      Envoy::Http::TestRequestHeaderMapImpl headers = {
          {"flag-header", "flag-header-value"},
          {"another-flag-header", "another-flag-header-value"},
          {"flag-header-2", "flag-header-2-value-old"},
          {"flag-header-3", "flag-header-3-value-old"},
          {"flag-header-4", "flag-header-4-value-old"},
          {":method", "GET"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "host"}};

      EXPECT_CALL(decoder_callbacks, mostSpecificPerFilterConfig())
          .WillRepeatedly(testing::Return(config.get()));

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
    }

    {
      Envoy::Http::TestResponseHeaderMapImpl headers = {
          {"flag-header", "flag-header-value"},
          {"another-flag-header", "another-flag-header-value"},
          {"flag-header-2", "flag-header-2-value-old"},
          {"flag-header-3", "flag-header-3-value-old"},
          {"flag-header-4", "flag-header-4-value-old"},
          {":status", "200"},
      };

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
    }
  }

  {
    HeaderMutation filter{};
    filter.setDecoderFilterCallbacks(decoder_callbacks);
    filter.setEncoderFilterCallbacks(encoder_callbacks);

    Envoy::Http::TestResponseHeaderMapImpl headers = {
        {"flag-header", "flag-header-value"},
        {"another-flag-header", "another-flag-header-value"},
        {"flag-header-2", "flag-header-2-value-old"},
        {"flag-header-3", "flag-header-3-value-old"},
        {"flag-header-4", "flag-header-4-value-old"},
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
  }
}

} // namespace
} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
