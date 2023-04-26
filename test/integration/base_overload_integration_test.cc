#include "test/integration/base_overload_integration_test.h"

#include "test/test_common/utility.h"

namespace Envoy {

void BaseOverloadIntegrationTest::setupOverloadManagerConfig(
    const envoy::config::overload::v3::OverloadAction& overload_action) {
  const std::string overload_config = R"EOF(
        refresh_interval:
          seconds: 0
          nanos: 1000000
        resource_monitors:
          - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
            typed_config:
              "@type": type.googleapis.com/test.common.config.DummyConfig
      )EOF";
  overload_manager_config_ =
      TestUtility::parseYaml<envoy::config::overload::v3::OverloadManager>(overload_config);
  *overload_manager_config_.add_actions() = overload_action;
}

} // namespace Envoy
