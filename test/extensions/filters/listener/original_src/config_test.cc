#include <numeric>

#include "envoy/config/filter/listener/original_src/v2alpha1/original_src.pb.h"
#include "envoy/config/filter/listener/original_src/v2alpha1/original_src.pb.validate.h"

#include "extensions/filters/listener/original_src/config.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {
namespace {

// In keeping with the class under test, it would have made sense to call this ConfigTest. However,
// when running coverage tests, that conflicts with tests elsewhere in the codebase.
class OriginalSrcConfigTest : public testing::Test {
public:
  Config makeConfigFromProto(
      const envoy::config::filter::listener::original_src::v2alpha1::OriginalSrc& proto_config) {
    return Config(proto_config);
  }
};

TEST_F(OriginalSrcConfigTest, TestUsePortTrue) {
  envoy::config::filter::listener::original_src::v2alpha1::OriginalSrc config_proto;
  config_proto.set_bind_port(true);
  auto config = makeConfigFromProto(config_proto);

  EXPECT_TRUE(config.usePort());
}

TEST_F(OriginalSrcConfigTest, TestUsePortFalse) {
  envoy::config::filter::listener::original_src::v2alpha1::OriginalSrc config_proto;
  config_proto.set_bind_port(false);
  auto config = makeConfigFromProto(config_proto);

  EXPECT_FALSE(config.usePort());
}

TEST_F(OriginalSrcConfigTest, TestUseMark0) {
  envoy::config::filter::listener::original_src::v2alpha1::OriginalSrc config_proto;
  config_proto.set_mark(0);
  auto config = makeConfigFromProto(config_proto);

  EXPECT_EQ(config.mark(), 0);
}

TEST_F(OriginalSrcConfigTest, TestUseMark1234) {
  envoy::config::filter::listener::original_src::v2alpha1::OriginalSrc config_proto;
  config_proto.set_mark(1234);
  auto config = makeConfigFromProto(config_proto);

  EXPECT_EQ(config.mark(), 1234);
}

TEST_F(OriginalSrcConfigTest, TestUseMarkMax) {
  envoy::config::filter::listener::original_src::v2alpha1::OriginalSrc config_proto;
  config_proto.set_mark(std::numeric_limits<uint32_t>::max());
  auto config = makeConfigFromProto(config_proto);

  EXPECT_EQ(config.mark(), std::numeric_limits<uint32_t>::max());
}

} // namespace
} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
