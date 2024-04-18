#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/http/early_header_mutation/header_mutation/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace HeaderMutation {
namespace {

using ProtoHeaderMutation =
    envoy::extensions::http::early_header_mutation::header_mutation::v3::HeaderMutation;

TEST(FactoryTest, FactoryTest) {
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;

  auto* factory = Registry::FactoryRegistry<Envoy::Http::EarlyHeaderMutationFactory>::getFactory(
      "envoy.http.early_header_mutation.header_mutation");
  ASSERT_NE(factory, nullptr);

  const std::string config = R"EOF(
  mutations:
  - remove: "flag-header"
  - append:
      header:
        key: "flag-header"
        value: "%REQ(ANOTHER-FLAG-HEADER)%"
      append_action: APPEND_IF_EXISTS_OR_ADD
  )EOF";

  ProtoHeaderMutation proto_mutation;
  TestUtility::loadFromYaml(config, proto_mutation);

  ProtobufWkt::Any any_config;
  any_config.PackFrom(proto_mutation);

  EXPECT_NE(nullptr, factory->createExtension(any_config, context));
}

} // namespace
} // namespace HeaderMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
