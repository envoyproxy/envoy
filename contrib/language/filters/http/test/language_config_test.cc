#include "test/mocks/server/factory_context.h"

#include "contrib/envoy/extensions/filters/http/language/v3alpha/language.pb.h"
#include "contrib/language/filters/http/source/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Language {

using LanguageFilterConfig = envoy::extensions::filters::http::language::v3alpha::Language;

using ConfigSharedPtr = std::shared_ptr<LanguageFilterConfig>;

class ConfigTest : public testing::Test {
public:
  void initializeFilter(const std::string& yaml) {
    envoy::extensions::filters::http::language::v3alpha::Language proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);

    factory_.createFilterFactoryFromProtoTyped(proto_config, "stats", factory_context_);
  }

private:
  LanguageFilterFactory factory_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
};

TEST_F(ConfigTest, ValidConfig) {
  const std::string yaml = R"EOF(
default_language: en
supported_languages: [fr, en-uk]
)EOF";

  EXPECT_NO_THROW(initializeFilter(yaml));
}

} // namespace Language
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
