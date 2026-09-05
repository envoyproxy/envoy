#include "envoy/registry/registry.h"

#include "source/extensions/http/early_header_mutation/exact_path_rewrite/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace ExactPathRewrite {
namespace {

TEST(FactoryTest, CreatesExtension) {
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  auto* factory = Registry::FactoryRegistry<Envoy::Http::EarlyHeaderMutationFactory>::getFactory(
      "envoy.http.early_header_mutation.exact_path_rewrite");
  ASSERT_NE(factory, nullptr);

  ProtoExactPathRewrite config;
  TestUtility::loadFromYaml(R"EOF(
host_header: ":authority"
hosts:
- domains: ["api.example.com"]
  rules:
  - exact_path: /api
    replacement_path: /api/
)EOF",
                            config);
  Protobuf::Any any_config;
  std::ignore = any_config.PackFrom(config);

  EXPECT_NE(nullptr, factory->createExtension(any_config, context));
}

TEST(FactoryTest, ThrowsOnInvalidConfiguration) {
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  auto* factory = Registry::FactoryRegistry<Envoy::Http::EarlyHeaderMutationFactory>::getFactory(
      "envoy.http.early_header_mutation.exact_path_rewrite");
  ASSERT_NE(factory, nullptr);

  ProtoExactPathRewrite config;
  TestUtility::loadFromYaml(R"EOF(
host_header: ":authority"
hosts:
- domains: ["api.example.com"]
  rules:
  - exact_path: /api?query
    replacement_path: /api/
)EOF",
                            config);
  Protobuf::Any any_config;
  std::ignore = any_config.PackFrom(config);

  EXPECT_THROW(factory->createExtension(any_config, context), EnvoyException);
}

} // namespace
} // namespace ExactPathRewrite
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
