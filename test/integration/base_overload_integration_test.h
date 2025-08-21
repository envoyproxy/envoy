#pragma once

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"

#include "test/integration/fake_resource_monitor.h"
#include "test/test_common/registry.h"

namespace Envoy {

class BaseOverloadIntegrationTest {
protected:
  void
  setupOverloadManagerConfig(const envoy::config::overload::v3::OverloadAction& overload_action);
  void
  setupOverloadManagerConfig(const envoy::config::overload::v3::LoadShedPoint& load_shed_point);

  void updateResource(double pressure) {
    auto* monitor = fake_resource_monitor_factory_.monitor();
    ASSERT(monitor != nullptr);
    monitor->setResourcePressure(pressure);
  }

  envoy::config::overload::v3::OverloadManager overload_manager_config_;
  FakeResourceMonitorFactory fake_resource_monitor_factory_;
  Registry::InjectFactory<Server::Configuration::ResourceMonitorFactory> inject_factory_{
      fake_resource_monitor_factory_};
};

} // namespace Envoy
