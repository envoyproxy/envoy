#include <numeric>

#include "envoy/config/filter/listener/original_src/v2alpha1/original_src.pb.h"
#include "envoy/config/filter/listener/original_src/v2alpha1/original_src.pb.validate.h"

#include "extensions/filters/listener/original_src/config.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {

class ConfigTest : public testing::Test {
public:
  Config makeConfigFromProto(
      const envoy::config::filter::listener::original_src::v2alpha1::OriginalSrc& proto_config) {
    return Config(proto_config);
  }
};

TEST_F(ConfigTest, TestUsePortTrue) {
  envoy::config::filter::listener::original_src::v2alpha1::OriginalSrc config_proto;
  config_proto.set_bind_port(true);
  auto config = makeConfigFromProto(config_proto);

  EXPECT_TRUE(config.usePort());
}

TEST_F(ConfigTest, TestUsePortFalse) {
  envoy::config::filter::listener::original_src::v2alpha1::OriginalSrc config_proto;
  config_proto.set_bind_port(false);
  auto config = makeConfigFromProto(config_proto);

  EXPECT_FALSE(config.usePort());
}

TEST_F(ConfigTest, TestUseMark0) {
  envoy::config::filter::listener::original_src::v2alpha1::OriginalSrc config_proto;
  config_proto.set_mark(0);
  auto config = makeConfigFromProto(config_proto);

  EXPECT_EQ(config.mark(), 0);
}

TEST_F(ConfigTest, TestUseMark1234) {
  envoy::config::filter::listener::original_src::v2alpha1::OriginalSrc config_proto;
  config_proto.set_mark(1234);
  auto config = makeConfigFromProto(config_proto);

  EXPECT_EQ(config.mark(), 1234);
}

TEST_F(ConfigTest, TestUseMarkMax) {
  envoy::config::filter::listener::original_src::v2alpha1::OriginalSrc config_proto;
  config_proto.set_mark(std::numeric_limits<uint32_t>::max());
  auto config = makeConfigFromProto(config_proto);

  EXPECT_EQ(config.mark(), std::numeric_limits<uint32_t>::max());
}
} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
