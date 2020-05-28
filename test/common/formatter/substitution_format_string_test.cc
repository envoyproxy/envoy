#include "envoy/config/core/v3/substitution_format_string.pb.validate.h"

#include "common/formatter/substitution_format_string.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Formatter {

class SubstitutionFormatStringUtilsTest : public ::testing::Test {
public:
  SubstitutionFormatStringUtilsTest() {
    absl::optional<uint32_t> response_code{200};
    EXPECT_CALL(stream_info_, responseCode()).WillRepeatedly(Return(response_code));
  }

  Http::TestRequestHeaderMapImpl request_headers_{{":method", "GET"}, {":path", "/bar/foo"}};
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  StreamInfo::MockStreamInfo stream_info_;
  std::string body_;

  envoy::config::core::v3::SubstitutionFormatString config_;
};

TEST_F(SubstitutionFormatStringUtilsTest, TestEmptyIsInvalid) {
  envoy::config::core::v3::SubstitutionFormatString empty_config;
  std::string err;
  EXPECT_FALSE(Validate(empty_config, &err));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigText) {
  const std::string yaml = R"EOF(
  text_format: "plain text, path=%REQ(:path)%, code=%RESPONSE_CODE%"
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter = SubstitutionFormatStringUtils::fromProtoConfig(config_);
  EXPECT_EQ("plain text, path=/bar/foo, code=200",
            formatter->format(request_headers_, response_headers_, response_trailers_, stream_info_,
                              body_));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigJson) {
  const std::string yaml = R"EOF(
  json_format:
    text: "plain text"
    path: "%REQ(:path)%"
    code: "%RESPONSE_CODE%"
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter = SubstitutionFormatStringUtils::fromProtoConfig(config_);
  const auto out_json = formatter->format(request_headers_, response_headers_, response_trailers_,
                                          stream_info_, body_);

  const std::string expected = R"EOF({
    "text": "plain text",
    "path": "/bar/foo",
    "code": 200
})EOF";
  EXPECT_TRUE(TestUtility::jsonStringEqual(out_json, expected));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestInvalidConfigs) {
  const std::vector<std::string> invalid_configs = {
      R"(
  json_format:
    field: true
)",
      R"(
  json_format:
    field: 200
)",
      R"(
  json_format:
    field:
      nest_field: "value"
)",
  };
  for (const auto& yaml : invalid_configs) {
    TestUtility::loadFromYaml(yaml, config_);
    EXPECT_THROW_WITH_MESSAGE(SubstitutionFormatStringUtils::fromProtoConfig(config_),
                              EnvoyException,
                              "Only string values are supported in the JSON access log format.");
  }
}

} // namespace Formatter
} // namespace Envoy
