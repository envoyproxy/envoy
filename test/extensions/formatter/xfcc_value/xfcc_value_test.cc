#include "envoy/config/core/v3/substitution_format_string.pb.validate.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {
namespace {

class XfccValueTest : public ::testing::Test {
public:
  StreamInfo::MockStreamInfo stream_info_;
  envoy::config::core::v3::SubstitutionFormatString config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(XfccValueTest, UnknownCommand) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%UNKNOWN_COMMAND%"
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter_or_error =
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("Not supported field in StreamInfo: UNKNOWN_COMMAND",
            formatter_or_error.status().message());
}

TEST_F(XfccValueTest, MissingSubcommand) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%XFCC_VALUE%"
)EOF";

  TestUtility::loadFromYaml(yaml, config_);

  EXPECT_THROW_WITH_MESSAGE(
      {
        auto error =
            Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
      },
      EnvoyException, "XFCC_VALUE command requires a subcommand");
}

TEST_F(XfccValueTest, UnsupportedSubcommand) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%XFCC_VALUE(unsupported_key)%"
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  EXPECT_THROW_WITH_MESSAGE(
      {
        auto error =
            Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
      },
      EnvoyException, "XFCC_VALUE command does not support subcommand: unsupported_key");
}

TEST_F(XfccValueTest, Test) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%XFCC_VALUE(uri)%"
)EOF";

  TestUtility::loadFromYaml(yaml, config_);
  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);

  {
    // No XFCC header.
    Http::TestRequestHeaderMapImpl headers{};

    EXPECT_EQ(formatter->formatWithContext({&headers}, stream_info_), "-");
  }

  {
    // Normal value.
    Http::TestRequestHeaderMapImpl headers{
        {"x-forwarded-client-cert", "By=test;URI=abc;DNS=example.com"}};
    EXPECT_EQ(formatter->formatWithContext({&headers}, stream_info_), "abc");
  }

  // Normal value with special characters.
  {
    Http::TestRequestHeaderMapImpl headers{
        {"x-forwarded-client-cert", "By=test;URI=\"a,b,c;\\\"e;f;g=x\";DNS=example.com"}};
    EXPECT_EQ(formatter->formatWithContext({&headers}, stream_info_), "a,b,c;\"e;f;g=x");
  }

  {
    // Multiple elements.
    Http::TestRequestHeaderMapImpl headers{
        {"x-forwarded-client-cert",
         "By=test;DNS=example.com,By=test;URI=\"a,b,c;\\\"e;f;g=x\";DNS=example.com"}};
    EXPECT_EQ(formatter->formatWithContext({&headers}, stream_info_), "a,b,c;\"e;f;g=x");
  }

  {
    // Unclosed quotes in XFCC header.
    Http::TestRequestHeaderMapImpl headers{
        {"x-forwarded-client-cert", "By=test;URI=\"abc;DNS=example.com"}};
    EXPECT_EQ(formatter->formatWithContext({&headers}, stream_info_), "-");
  }

  {
    // No required key.
    Http::TestRequestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test;DNS=example.com"}};
    EXPECT_EQ(formatter->formatWithContext({&headers}, stream_info_), "-");
  }
}

} // namespace
} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
