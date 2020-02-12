#include "envoy/registry/registry.h"

#include "extensions/stat_sinks/metrics_service/config.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {
namespace {

// Test that the deprecated extension name still functions.
TEST(MetricsServiceConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.metrics_service";

  ASSERT_NE(nullptr, Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(
                         deprecated_name));
}

} // namespace
} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
