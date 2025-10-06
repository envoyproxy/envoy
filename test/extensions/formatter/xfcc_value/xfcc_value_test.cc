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
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(XfccValueTest, UnknownCommand) {
  auto formatter_or_error = Envoy::Formatter::SubstitutionFormatParser::parse("%UNKNOWN_COMMAND%");
  EXPECT_EQ("Not supported field in StreamInfo: UNKNOWN_COMMAND",
            formatter_or_error.status().message());
}

TEST_F(XfccValueTest, MissingSubcommand) {
  EXPECT_THROW_WITH_MESSAGE(
      { auto error = Envoy::Formatter::SubstitutionFormatParser::parse("%XFCC_VALUE%"); },
      EnvoyException, "XFCC_VALUE command requires a subcommand");
}

TEST_F(XfccValueTest, UnsupportedSubcommand) {
  EXPECT_THROW_WITH_MESSAGE(
      {
        auto error =
            Envoy::Formatter::SubstitutionFormatParser::parse("%XFCC_VALUE(unsupported_key)%");
      },
      EnvoyException, "XFCC_VALUE command does not support subcommand: unsupported_key");
}

TEST_F(XfccValueTest, Test) {
  auto formatter =
      std::move(Envoy::Formatter::SubstitutionFormatParser::parse("%XFCC_VALUE(uri)%").value()[0]);

  {
    // No XFCC header.
    Http::TestRequestHeaderMapImpl headers{};
    EXPECT_TRUE(formatter->formatValueWithContext({&headers}, stream_info_).has_null_value());
  }

  {
    // Normal value.
    Http::TestRequestHeaderMapImpl headers{
        {"x-forwarded-client-cert", "By=test;URI=abc;DNS=example.com"}};
    EXPECT_EQ(formatter->formatValueWithContext({&headers}, stream_info_).string_value(), "abc");
  }

  // Normal value with special characters.
  {
    Http::TestRequestHeaderMapImpl headers{
        {"x-forwarded-client-cert", R"(By=test;URI="a,b,c;\"e;f;g=x";DNS=example.com)"}};
    EXPECT_EQ(formatter->formatValueWithContext({&headers}, stream_info_).string_value(),
              R"(a,b,c;"e;f;g=x)");
  }

  {
    // Multiple elements.
    Http::TestRequestHeaderMapImpl headers{
        {"x-forwarded-client-cert",
         R"(By=test;DNS=example.com,By=test;URI="a,b,c;\"e;f;g=x";DNS=example.com)"}};
    EXPECT_EQ(formatter->formatValueWithContext({&headers}, stream_info_).string_value(),
              R"(a,b,c;"e;f;g=x)");
  }

  {
    // With escaped backslash.
    Http::TestRequestHeaderMapImpl headers{
        {"x-forwarded-client-cert", R"(By=test;DNS=example.com,By=test;URI="\\";DNS=example.com)"}};
    EXPECT_EQ(formatter->formatValueWithContext({&headers}, stream_info_).string_value(), R"(\)");
  }

  {
    // With escaped backslash and escaped quote.
    Http::TestRequestHeaderMapImpl headers{
        {"x-forwarded-client-cert",
         R"(By=test;DNS=example.com,By=test;URI="\\\"";DNS=example.com)"}};
    EXPECT_EQ(formatter->formatValueWithContext({&headers}, stream_info_).string_value(), R"(\")");
  }

  {
    // Unclosed quotes in XFCC header.
    Http::TestRequestHeaderMapImpl headers{
        {"x-forwarded-client-cert", R"(By=test;URI="abc;DNS=example.com)"}};
    EXPECT_TRUE(formatter->formatValueWithContext({&headers}, stream_info_).has_null_value());
  }

  {
    // No required key.
    Http::TestRequestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test;DNS=example.com"}};
    EXPECT_TRUE(formatter->formatValueWithContext({&headers}, stream_info_).has_null_value());
  }
}

} // namespace
} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
