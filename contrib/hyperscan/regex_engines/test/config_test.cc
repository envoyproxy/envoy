#include "test/mocks/server/factory_context.h"

#include "contrib/hyperscan/regex_engines/source/config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Regex {
namespace Hyperscan {

class ConfigTest : public ::testing::Test {
protected:
  void setup() {
    envoy::extensions::regex_engines::hyperscan::v3alpha::Hyperscan config;
    Config factory;
    engine_ = factory.createEngine(config, context_);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Envoy::Regex::EnginePtr engine_;
};

#ifdef HYPERSCAN_DISABLED
// Verify that incompatible architecture will cause a throw.
TEST_F(ConfigTest, IncompatibleArchitecture) {
  EXPECT_THROW_WITH_MESSAGE(setup(), EnvoyException,
                            "X86_64 architecture is required for Hyperscan.");
}
#else
// Verify that matching will be performed successfully.
TEST_F(ConfigTest, Regex) {
  setup();

  Envoy::Regex::CompiledMatcherPtr matcher = engine_->matcher("^/asdf/.+");

  EXPECT_TRUE(matcher->match("/asdf/1"));
  EXPECT_FALSE(matcher->match("/ASDF/1"));
};
#endif

} // namespace Hyperscan
} // namespace Regex
} // namespace Extensions
} // namespace Envoy
