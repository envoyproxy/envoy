#include "gtest/gtest.h"
#include "library/common/bridge/utility.h"
#include "library/common/data/utility.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Bridge {

TEST(EnvoyMapConvenientInitializerTest, FromCppToCEmpty) {
  const auto map = Utility::makeEnvoyMap({});

  EXPECT_EQ(map.length, 0);
  release_envoy_map(map);
}

TEST(EnvoyMapConvenientInitializerTest, FromCppToC) {
  const auto map = Utility::makeEnvoyMap({{"foo", "bar"}});

  EXPECT_EQ(Data::Utility::copyToString(map.entries[0].key), "foo");
  EXPECT_EQ(Data::Utility::copyToString(map.entries[0].value), "bar");
  EXPECT_EQ(map.length, 1);
  release_envoy_map(map);
}

} // namespace Bridge
} // namespace Envoy
