#include "test/integration/base_overload_integration_test.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace {

envoy::config::overload::v3::OverloadManager getBaseOverloadManagerConfig() {
  return TestUtility::parseYaml<envoy::config::overload::v3::OverloadManager>(R"EOF(
        refresh_interval:
          seconds: 0
          nanos: 1000000
        resource_monitors:
          - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
            typed_config:
              "@type": type.googleapis.com/test.common.config.DummyConfig
      )EOF");
}

} // namespace

void BaseOverloadIntegrationTest::setupOverloadManagerConfig(
    const envoy::config::overload::v3::OverloadAction& overload_action) {
  overload_manager_config_ = getBaseOverloadManagerConfig();
  *overload_manager_config_.add_actions() = overload_action;
}

void BaseOverloadIntegrationTest::setupOverloadManagerConfig(
    const envoy::config::overload::v3::LoadShedPoint& load_shed_point) {
  overload_manager_config_ = getBaseOverloadManagerConfig();
  *overload_manager_config_.add_loadshed_points() = load_shed_point;
}

} // namespace Envoy
