#include "gtest/gtest.h"
#include "library/common/envoy_mobile_main_common.h"

namespace Envoy {

TEST(MobileMainCommonTest, SignalHandlingFalse) {
  std::vector<const char*> envoy_argv{
      "envoy", "--config-yaml",
      "{\"layered_runtime\":{\"layers\":[{\"name\":\"static_layer_0\",\"static_layer\":{"
      "\"overload\":{\"global_downstream_max_connections\":50000}}}]}}",
      nullptr};
  MobileMainCommon main_common{3, &envoy_argv[0]};
  ASSERT_FALSE(main_common.server()->options().signalHandlingEnabled());
}

} // namespace Envoy
