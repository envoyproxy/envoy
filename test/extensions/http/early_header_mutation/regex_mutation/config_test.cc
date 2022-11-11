#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/http/early_header_mutation/regex_mutation/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace RegexMutation {
namespace {

using ProtoRegexMutation =
    envoy::extensions::http::early_header_mutation::regex_mutation::v3::RegexMutation;

TEST(FactoryTest, FactoryTest) {
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;

  auto* factory = Registry::FactoryRegistry<Envoy::Http::EarlyHeaderMutationFactory>::getFactory(
      "envoy.http.early_header_mutation.regex_mutation");
  ASSERT_NE(factory, nullptr);

  const std::string config = R"EOF(
  header_mutations:
    - header: "foo"
      regex_rewrite:
        pattern:
          regex: "foo"
        substitution: "bar"
    - header: "foo"
      regex_rewrite:
        pattern:
          regex: "bar"
        substitution: "qux"
    - header: "foo"
      rename: "bar"
      regex_rewrite:
        pattern:
          regex: "^(.*)$"
        substitution: "\\1"
  )EOF";

  ProtoRegexMutation proto_mutation;
  TestUtility::loadFromYaml(config, proto_mutation);

  ProtobufWkt::Any any_config;
  any_config.PackFrom(proto_mutation);

  EXPECT_NE(nullptr, factory->createExtension(any_config, context));
}

} // namespace
} // namespace RegexMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
