#include "envoy/config/core/v3/substitution_format_string.pb.validate.h"

#include "common/formatter/substitution_format_string.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

class ReqWithoutQueryTest : public ::testing::Test {
public:
  Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "GET"},
      {":path", "/request/path?secret=parameter"},
      {"x-envoy-original-path", "/original/path?secret=parameter"}};
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  StreamInfo::MockStreamInfo stream_info_;
  std::string body_;

  envoy::config::core::v3::SubstitutionFormatString config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(ReqWithoutQueryTest, TestStripQueryString) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_WITHOUT_QUERY(:PATH)%"
  formatters:
    - name: envoy.formatter.req_without_query
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      ::Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_.api());
  EXPECT_EQ("/request/path", formatter->format(request_headers_, response_headers_,
                                               response_trailers_, stream_info_, body_));
}

TEST_F(ReqWithoutQueryTest, TestSelectMainHeader) {

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_WITHOUT_QUERY(X-ENVOY-ORIGINAL-PATH?:PATH)%"
  formatters:
    - name: envoy.formatter.req_without_query
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      ::Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_.api());
  EXPECT_EQ("/original/path", formatter->format(request_headers_, response_headers_,
                                                response_trailers_, stream_info_, body_));
}

TEST_F(ReqWithoutQueryTest, TestSelectAlternativeHeader) {

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_WITHOUT_QUERY(X-NON-EXISTING-HEADER?:PATH)%"
  formatters:
    - name: envoy.formatter.req_without_query
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      ::Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_.api());
  EXPECT_EQ("/request/path", formatter->format(request_headers_, response_headers_,
                                               response_trailers_, stream_info_, body_));
}

TEST_F(ReqWithoutQueryTest, TestTruncateHeader) {

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_WITHOUT_QUERY(:PATH):5%"
  formatters:
    - name: envoy.formatter.req_without_query
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      ::Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_.api());
  EXPECT_EQ("/requ", formatter->format(request_headers_, response_headers_, response_trailers_,
                                       stream_info_, body_));
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
