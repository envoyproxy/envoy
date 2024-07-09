#include "test/mocks/stream_info/mocks.h"

#include "contrib/generic_proxy/filters/network/source/access_log.h"
#include "contrib/generic_proxy/filters/network/test/fake_codec.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace {

TEST(AccessLogFormatterTest, AccessLogFormatterTest) {

  {
    // Test for %METHOD%.
    FormatterContext context;
    Envoy::Formatter::FormatterBaseImpl<FormatterContext> formatter("%METHOD%");
    StreamInfo::MockStreamInfo stream_info;

    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "-");

    FakeStreamCodecFactory::FakeRequest request;
    request.method_ = "FAKE_METHOD";
    context.request_ = &request;

    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "FAKE_METHOD");
  }

  {
    // Test for %HOST%.
    FormatterContext context;
    Envoy::Formatter::FormatterBaseImpl<FormatterContext> formatter("%HOST%");
    StreamInfo::MockStreamInfo stream_info;

    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "-");

    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "FAKE_HOST";
    context.request_ = &request;

    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "FAKE_HOST");
  }

  {
    // Test for %PATH%.
    FormatterContext context;
    Envoy::Formatter::FormatterBaseImpl<FormatterContext> formatter("%PATH%");
    StreamInfo::MockStreamInfo stream_info;

    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "-");

    FakeStreamCodecFactory::FakeRequest request;
    request.path_ = "FAKE_PATH";
    context.request_ = &request;

    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "FAKE_PATH");
  }

  {
    // Test for %PROTOCOL%.
    FormatterContext context;
    Envoy::Formatter::FormatterBaseImpl<FormatterContext> formatter("%PROTOCOL%");
    StreamInfo::MockStreamInfo stream_info;

    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "-");

    FakeStreamCodecFactory::FakeRequest request;
    request.protocol_ = "FAKE_PROTOCOL";
    context.request_ = &request;
    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "FAKE_PROTOCOL");
  }

  {
    // Test for %REQUEST_PROPERTY%.
    FormatterContext context;
    Envoy::Formatter::FormatterBaseImpl<FormatterContext> formatter("%REQUEST_PROPERTY(FAKE_KEY)%");
    StreamInfo::MockStreamInfo stream_info;

    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "-");

    FakeStreamCodecFactory::FakeRequest request;

    context.request_ = &request;
    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "-");

    request.data_["FAKE_KEY"] = "FAKE_VALUE";

    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "FAKE_VALUE");
  }

  {
    // Test for %RESPONSE_PROPERTY%.
    FormatterContext context;
    Envoy::Formatter::FormatterBaseImpl<FormatterContext> formatter(
        "%RESPONSE_PROPERTY(FAKE_KEY)%");
    StreamInfo::MockStreamInfo stream_info;

    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "-");

    FakeStreamCodecFactory::FakeResponse response;

    context.response_ = &response;
    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "-");

    response.data_["FAKE_KEY"] = "FAKE_VALUE";

    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "FAKE_VALUE");
  }

  {
    // Test for %RESPONSE_CODE%.
    // This command overrides the default one which is defined in the
    // source/common/formatter/stream_info_formatter.cc.
    FormatterContext context;
    Envoy::Formatter::FormatterBaseImpl<FormatterContext> formatter("%RESPONSE_CODE%");
    StreamInfo::MockStreamInfo stream_info;

    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "-");

    FakeStreamCodecFactory::FakeResponse response;
    response.status_ = {-1234, false};
    context.response_ = &response;

    EXPECT_EQ(formatter.formatWithContext(context, stream_info), "-1234");
  }
}

} // namespace
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
