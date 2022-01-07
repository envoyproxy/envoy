#include "envoy/registry/registry.h"

#include "source/extensions/stat_sinks/metrics_service/config.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {
namespace {

// Test that the extension name is disabled by default.
// TODO(zuercher): remove when envoy.deprecated_features.allow_deprecated_extension_names is removed
TEST(MetricsServiceConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  ASSERT_EQ(nullptr, Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(
                         "envoy.metrics_service"));
}

} // namespace
} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
