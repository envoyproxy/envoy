#include "envoy/config/core/v3/extension.pb.h"

#include "source/common/protobuf/utility.h"

#include "test/test_common/status_utility.h"

#include "contrib/envoy/extensions/network/connection_balance/dlb/v3alpha/dlb.pb.h"
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
}

TEST_F(DlbConnectionBalanceFactoryTest, MakeCustomConfig) {
  envoy::config::core::v3::TypedExtensionConfig typed_config;

  envoy::extensions::network::connection_balance::dlb::v3alpha::Dlb dlb;
  dlb.set_id(10);

  makeDlbConnectionBalanceConfig(typed_config, dlb);
  verifyDlbConnectionBalanceConfigAndUnpack(typed_config, dlb);
  EXPECT_EQ(10, dlb.id());
}

} // namespace Dlb
} // namespace Extensions
} // namespace Envoy
