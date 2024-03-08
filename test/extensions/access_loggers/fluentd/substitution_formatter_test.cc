#include "envoy/registry/registry.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/json/json_loader.h"
#include "source/extensions/access_loggers/fluentd/substitution_formatter.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Fluentd {
namespace {

TEST(FluentdFormatterImplTest, FormatMsgpack) {
  ProtobufWkt::Struct log_struct;
  (*log_struct.mutable_fields())["Message"].set_string_value("SomeValue");
  (*log_struct.mutable_fields())["LogType"].set_string_value("%ACCESS_LOG_TYPE%");

  auto json_formatter =
      Formatter::SubstitutionFormatStringUtils::createJsonFormatter(log_struct, true, false, true);

  auto fluentd_formatter = FluentdFormatterImpl(std::move(json_formatter));
  auto expected_json = "{\"Message\":\"SomeValue\",\"LogType\":\"NotSet\"}";
  auto expected_msgpack = Json::Factory::jsonToMsgpack(expected_json);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  std::vector<uint8_t> msgpack = fluentd_formatter.format({}, stream_info);
  std::string expected_msgpack_str(expected_msgpack.begin(), expected_msgpack.end());
  std::string msgpack_str(msgpack.begin(), msgpack.end());
  EXPECT_EQ(expected_msgpack_str, msgpack_str);
}

} // namespace
} // namespace Fluentd
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
