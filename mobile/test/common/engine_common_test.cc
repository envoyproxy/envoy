#include "extension_registry.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/common/engine_common.h"

namespace Envoy {

TEST(EngineCommonTest, SignalHandlingFalse) {
  ExtensionRegistry::registerFactories();
  auto options = std::make_unique<Envoy::OptionsImplBase>();

  Platform::EngineBuilder builder;
  options->setConfigProto(builder.generateBootstrap());
  EngineCommon main_common{std::move(options)};
  ASSERT_FALSE(main_common.server()->options().signalHandlingEnabled());
}

} // namespace Envoy
