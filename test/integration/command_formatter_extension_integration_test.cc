#include "test/common/formatter/command_extension.h"
#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::HasSubstr;

namespace Envoy {
namespace Formatter {

class CommandFormatterExtensionIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  CommandFormatterExtensionIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {}
};

TEST_F(CommandFormatterExtensionIntegrationTest, BasicExtension) {
  autonomous_upstream_ = true;
  TestCommandFactory factory;
  Registry::InjectFactory<CommandParserFactory> command_register(factory);
  std::vector<envoy::config::core::v3::TypedExtensionConfig> formatters;
  envoy::config::core::v3::TypedExtensionConfig typed_config;
  ProtobufWkt::StringValue config;

  typed_config.set_name("envoy.formatter.TestFormatter");
  typed_config.mutable_typed_config()->PackFrom(config);
  formatters.push_back(typed_config);

  useAccessLog("%COMMAND_EXTENSION()%", formatters);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.1\r\nHost: host\r\n\r\n",
                                &response, true);
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("TestFormatter"));
}

} // namespace Formatter
} // namespace Envoy
