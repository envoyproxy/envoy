#include "envoy/config/core/v3/extension.pb.h"

#include "source/common/protobuf/utility.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/status_utility.h"

#include "contrib/network/connection_balance/dlb/source/connection_balancer_impl.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Dlb {

class DlbConnectionBalanceFactoryTest : public testing::Test {
protected:
  // Create a default DLB connection balance typed config.
  static void makeDlbConnectionBalanceConfig(
      envoy::config::core::v3::TypedExtensionConfig& typed_config,
      envoy::extensions::network::connection_balance::dlb::v3alpha::Dlb& dlb) {
    typed_config.mutable_typed_config()->PackFrom(dlb);
    typed_config.set_name("envoy.network.connection_balance.dlb");
  }

  // Verify typed config is dlb, and unpack to dlb object.
  static void verifyDlbConnectionBalanceConfigAndUnpack(
      envoy::config::core::v3::TypedExtensionConfig& typed_config,
      envoy::extensions::network::connection_balance::dlb::v3alpha::Dlb& dlb) {
    EXPECT_EQ(typed_config.name(), "envoy.network.connection_balance.dlb");
    EXPECT_EQ(typed_config.typed_config().type_url(),
              "type.googleapis.com/"
              "envoy.extensions.network.connection_balance.dlb.v3alpha.Dlb");
    ASSERT_OK(MessageUtil::unpackToNoThrow(typed_config.typed_config(), dlb));
  }
};

TEST_F(DlbConnectionBalanceFactoryTest, MakeDefaultConfig) {
  envoy::config::core::v3::TypedExtensionConfig typed_config;
  envoy::extensions::network::connection_balance::dlb::v3alpha::Dlb dlb;
  makeDlbConnectionBalanceConfig(typed_config, dlb);
  verifyDlbConnectionBalanceConfigAndUnpack(typed_config, dlb);
  EXPECT_EQ(0, dlb.id());
  EXPECT_EQ(0, dlb.max_retries());
  EXPECT_EQ(false, dlb.is_exact_fallback());
}

TEST_F(DlbConnectionBalanceFactoryTest, MakeCustomConfig) {
  envoy::config::core::v3::TypedExtensionConfig typed_config;

  envoy::extensions::network::connection_balance::dlb::v3alpha::Dlb dlb;
  dlb.set_id(10);
  dlb.set_max_retries(12);
  dlb.set_is_exact_fallback(true);

  makeDlbConnectionBalanceConfig(typed_config, dlb);
  verifyDlbConnectionBalanceConfigAndUnpack(typed_config, dlb);
  EXPECT_EQ(10, dlb.id());
  EXPECT_EQ(12, dlb.max_retries());
  EXPECT_EQ(true, dlb.is_exact_fallback());
}

TEST_F(DlbConnectionBalanceFactoryTest, EmptyProto) {
  DlbConnectionBalanceFactory factory;
  EXPECT_NE(nullptr,
            dynamic_cast<envoy::extensions::network::connection_balance::dlb::v3alpha::Dlb*>(
                factory.createEmptyConfigProto().get()));
}

TEST_F(DlbConnectionBalanceFactoryTest, MockDetectDlbDevice) {
  envoy::extensions::network::connection_balance::dlb::v3alpha::Dlb dlb;
  dlb.set_id(1);

  const std::string& dlb_path = TestEnvironment::temporaryDirectory();
  TestEnvironment::createPath(dlb_path);
  const std::ofstream file(dlb_path + "/" + "dlb6");

  const auto& result = detectDlbDevice(dlb.id(), dlb_path);
  EXPECT_EQ(true, result.has_value());
  EXPECT_EQ(6, result.value());
  TestEnvironment::removePath(dlb_path);
}

TEST_F(DlbConnectionBalanceFactoryTest, Fallback) {
  DlbConnectionBalanceFactory factory;
  const std::string& test_message = "this is a test for dlb";
  EXPECT_LOG_CONTAINS("warn",
                      fmt::format("error: {}, fallback to Nop Connection Balance", test_message),
                      factory.fallback(false, test_message));

  EXPECT_LOG_CONTAINS("warn",
                      fmt::format("error: {}, fallback to Exact Connection Balance", test_message),
                      factory.fallback(true, test_message));
}

#ifndef DLB_DISABLED

using testing::HasSubstr;

TEST_F(DlbConnectionBalanceFactoryTest, MakeFromDefaultProto) {
  envoy::config::core::v3::TypedExtensionConfig typed_config;
  DlbConnectionBalanceFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  envoy::extensions::network::connection_balance::dlb::v3alpha::Dlb dlb;
  makeDlbConnectionBalanceConfig(typed_config, dlb);

  EXPECT_LOG_CONTAINS("warn",
                      "error: no available dlb hardware, fallback to Nop Connection Balance",
                      factory.createConnectionBalancerFromProto(typed_config, context));

  EXPECT_EQ(std::make_shared<Network::NopConnectionBalancerImpl>(),
            factory.createConnectionBalancerFromProto(typed_config, context));
}

TEST_F(DlbConnectionBalanceFactoryTest, MakeFromExactFallbackProto) {
  envoy::config::core::v3::TypedExtensionConfig typed_config;
  DlbConnectionBalanceFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  envoy::extensions::network::connection_balance::dlb::v3alpha::Dlb dlb;
  dlb.set_is_exact_fallback(true);
  makeDlbConnectionBalanceConfig(typed_config, dlb);

  EXPECT_LOG_CONTAINS("warn", "fallback to Exact Connection Balance",
                      factory.createConnectionBalancerFromProto(typed_config, context));

  EXPECT_EQ(std::make_shared<Network::ExactConnectionBalancerImpl>(),
            factory.createConnectionBalancerFromProto(typed_config, context));
}

TEST_F(DlbConnectionBalanceFactoryTest, TooManyThreads) {
  envoy::config::core::v3::TypedExtensionConfig typed_config;
  DlbConnectionBalanceFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  context.options_.concurrency_ = 33;

  envoy::extensions::network::connection_balance::dlb::v3alpha::Dlb dlb;
  makeDlbConnectionBalanceConfig(typed_config, dlb);
  EXPECT_LOG_CONTAINS("warn", "Dlb connection balanncer only supports up to 32 worker threads",
                      factory.createConnectionBalancerFromProto(typed_config, context));
  EXPECT_EQ(std::make_shared<Network::NopConnectionBalancerImpl>(),
            factory.createConnectionBalancerFromProto(typed_config, context));
}
#endif

} // namespace Dlb
} // namespace Extensions
} // namespace Envoy
