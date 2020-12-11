#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Verify KillRequest filter is not built into Envoy by default but in compile_time_options
TEST(ExtraExtensionsTest, KillRequestFilterIsNotBuiltByDefault) {
  auto* factory = Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::
      getFactoryByType("envoy.extensions.filters.http.kill_request.v3.KillRequest");
  if (TestEnvironment::getOptionalEnvVar("ENVOY_HAS_EXTRA_EXTENSIONS").value_or("") == "true") {
    EXPECT_NE(nullptr, factory);
  } else {
    EXPECT_EQ(nullptr, factory);
  }
}

} // namespace
} // namespace Envoy
