#include "source/common/formatter/substitution_formatter.h"
#include "source/extensions/filters/network/generic_proxy/access_log.h"

#include "test/extensions/filters/network/generic_proxy/fake_codec.h"
#include "test/mocks/stream_info/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace {

TEST(GenericStatusCodeFormatterProviderTest, GenericStatusCodeFormatterProviderTest) {
  FormatterContextExtension context;
  GenericStatusCodeFormatterProvider formatter;
  StreamInfo::MockStreamInfo stream_info;

  EXPECT_EQ(formatter.format(Formatter::Context().setExtension(context), stream_info),
            absl::nullopt);
  EXPECT_TRUE(formatter.formatValue(Formatter::Context().setExtension(context), stream_info)
                  .has_null_value());

  FakeStreamCodecFactory::FakeResponse response;
  response.status_ = {1234, false};
  context.response_ = &response;

  EXPECT_EQ(formatter.format(Formatter::Context().setExtension(context), stream_info).value(),
            "1234");
  EXPECT_EQ(
      formatter.formatValue(Formatter::Context().setExtension(context), stream_info).number_value(),
      1234.0);
}

TEST(StringValueFormatterProviderTest, StringValueFormatterProviderTest) {
  {

    FormatterContextExtension context;
    StringValueFormatterProvider formatter(
        [](const Formatter::Context& context,
           const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
          CHECK_DATA_OR_RETURN(context, request_, absl::nullopt);
          return std::string(checked_data->request_->path());
        },
        9);
    StreamInfo::MockStreamInfo stream_info;

    EXPECT_EQ(formatter.format(Formatter::Context().setExtension(context), stream_info),
              absl::nullopt);
    EXPECT_TRUE(formatter.formatValue(Formatter::Context().setExtension(context), stream_info)
                    .has_null_value());

    FakeStreamCodecFactory::FakeRequest request;
    request.path_ = "ANYTHING";
    context.request_ = &request;

    EXPECT_EQ(formatter.format(Formatter::Context().setExtension(context), stream_info).value(),
              "ANYTHING");
    EXPECT_EQ(formatter.formatValue(Formatter::Context().setExtension(context), stream_info)
                  .string_value(),
              "ANYTHING");

    request.path_ = "ANYTHING_LONGER_THAN_9";
    EXPECT_EQ(formatter.format(Formatter::Context().setExtension(context), stream_info).value(),
              "ANYTHING_");
    EXPECT_EQ(formatter.formatValue(Formatter::Context().setExtension(context), stream_info)
                  .string_value(),
              "ANYTHING_");
  }
}

TEST(AccessLogFormatterTest, AccessLogFormatterTest) {

  std::vector<Formatter::CommandParserPtr> commands;
  commands.push_back(createGenericProxyCommandParser());

  {
    // Test for %METHOD%.
    FormatterContextExtension context;
    auto formatter = *Envoy::Formatter::FormatterImpl::create("%METHOD%", false, commands);
    StreamInfo::MockStreamInfo stream_info;

    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info), "-");

    FakeStreamCodecFactory::FakeRequest request;
    request.method_ = "FAKE_METHOD";
    context.request_ = &request;

    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info),
              "FAKE_METHOD");
  }

  {
    // Test for %HOST%.
    FormatterContextExtension context;
    auto formatter = *Envoy::Formatter::FormatterImpl::create("%HOST%", false, commands);
    StreamInfo::MockStreamInfo stream_info;

    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info), "-");

    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "FAKE_HOST";
    context.request_ = &request;

    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info),
              "FAKE_HOST");
  }

  {
    // Test for %PATH%.
    FormatterContextExtension context;
    auto formatter = *Envoy::Formatter::FormatterImpl::create("%PATH%", false, commands);
    StreamInfo::MockStreamInfo stream_info;

    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info), "-");

    FakeStreamCodecFactory::FakeRequest request;
    request.path_ = "FAKE_PATH";
    context.request_ = &request;

    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info),
              "FAKE_PATH");
  }

  {
    // Test for %PROTOCOL%.
    FormatterContextExtension context;
    auto formatter = *Envoy::Formatter::FormatterImpl::create("%PROTOCOL%", false, commands);
    StreamInfo::MockStreamInfo stream_info;

    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info), "-");

    FakeStreamCodecFactory::FakeRequest request;
    request.protocol_ = "FAKE_PROTOCOL";
    context.request_ = &request;
    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info),
              "FAKE_PROTOCOL");
  }

  {
    // Test for %REQUEST_PROPERTY%.
    FormatterContextExtension context;
    auto formatter =
        *Envoy::Formatter::FormatterImpl::create("%REQUEST_PROPERTY(FAKE_KEY)%", false, commands);
    StreamInfo::MockStreamInfo stream_info;

    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info), "-");

    FakeStreamCodecFactory::FakeRequest request;

    context.request_ = &request;
    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info), "-");

    request.data_["FAKE_KEY"] = "FAKE_VALUE";

    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info),
              "FAKE_VALUE");
  }

  {
    // Test for %RESPONSE_PROPERTY%.
    FormatterContextExtension context;
    auto formatter =
        *Envoy::Formatter::FormatterImpl::create("%RESPONSE_PROPERTY(FAKE_KEY)%", false, commands);
    StreamInfo::MockStreamInfo stream_info;

    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info), "-");

    FakeStreamCodecFactory::FakeResponse response;

    context.response_ = &response;
    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info), "-");

    response.data_["FAKE_KEY"] = "FAKE_VALUE";

    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info),
              "FAKE_VALUE");
  }

  {
    // Test for %GENERIC_RESPONSE_CODE%.
    FormatterContextExtension context;
    auto formatter =
        *Envoy::Formatter::FormatterImpl::create("%GENERIC_RESPONSE_CODE%", false, commands);
    StreamInfo::MockStreamInfo stream_info;

    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info), "-");

    FakeStreamCodecFactory::FakeResponse response;
    response.status_ = {-1234, false};
    context.response_ = &response;

    EXPECT_EQ(formatter->format(Formatter::Context().setExtension(context), stream_info), "-1234");
  }
}

} // namespace
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
