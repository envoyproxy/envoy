#include "gtest/gtest.h"
#include "library/common/engine_common.h"

namespace Envoy {

TEST(EngineCommonTest, SignalHandlingFalse) {
  auto options = std::make_unique<Envoy::OptionsImpl>();
  options->setConfigYaml(
      "{\"layered_runtime\":{\"layers\":[{\"name\":\"static_layer_0\",\"static_layer\":{"
      "\"overload\":{\"global_downstream_max_connections\":50000}}}]}}");
  EngineCommon main_common{std::move(options)};
  ASSERT_FALSE(main_common.server()->options().signalHandlingEnabled());
}

} // namespace Envoy
