#include "source/common/formatter/substitution_format_string.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

class DataSourceFormatterTest : public ::testing::Test {
public:
  envoy::config::core::v3::SubstitutionFormatString config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  StreamInfo::MockStreamInfo stream_info_;
  ::Envoy::Formatter::Context formatter_context_;

  ::Envoy::Formatter::FormatterPtr makeFormatter(const std::string& yaml) {
    TestUtility::loadFromYaml(yaml, config_);
    return THROW_OR_RETURN_VALUE(
        ::Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_),
        ::Envoy::Formatter::FormatterPtr);
  }
};

// A known name resolves to the configured DataSource value.
TEST_F(DataSourceFormatterTest, KnownNameResolvesToValue) {
  const std::string yaml = R"EOF(
text_format_source:
  inline_string: "prefix-%DATASOURCE(my-key)%-suffix"
formatters:
- name: envoy.formatter.datasource
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.datasource.v3.DataSource
    datasources:
      my-key:
        inline_string: "hello"
)EOF";

  auto formatter = makeFormatter(yaml);
  EXPECT_EQ("prefix-hello-suffix", formatter->format(formatter_context_, stream_info_));
}

TEST_F(DataSourceFormatterTest, UnknownNameThrows) {
  const std::string yaml = R"EOF(
text_format_source:
  inline_string: "%DATASOURCE(my-key)% %DATASOURCE(unknown)%"
formatters:
- name: envoy.formatter.datasource
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.datasource.v3.DataSource
    datasources:
      my-key:
        inline_string: "hello"
)EOF";

  EXPECT_THROW_WITH_MESSAGE(makeFormatter(yaml), EnvoyException,
                            "Not supported field in StreamInfo: DATASOURCE");
}

TEST_F(DataSourceFormatterTest, MultipleDatasources) {
  const std::string yaml = R"EOF(
text_format_source:
  inline_string: "%DATASOURCE(x)%:%DATASOURCE(y)%"
formatters:
- name: envoy.formatter.datasource
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.datasource.v3.DataSource
    datasources:
      x:
        inline_string: "alpha"
      y:
        inline_string: "bravo"
)EOF";

  auto formatter = makeFormatter(yaml);
  EXPECT_EQ("alpha:bravo", formatter->format(formatter_context_, stream_info_));
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
