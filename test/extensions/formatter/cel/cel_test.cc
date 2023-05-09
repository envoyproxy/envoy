#include "envoy/config/core/v3/substitution_format_string.pb.validate.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

class CELFormatterTest : public ::testing::Test {
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

TEST_F(CELFormatterTest, TestStripQueryString) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%CEL(request.headers[':method'])%"
  formatters:
    - name: envoy.formatter.cel
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.cel.v3.cel
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("GET", formatter->format(request_headers_, response_headers_, response_trailers_,
                                     stream_info_, body_));
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
