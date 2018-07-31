#pragma once

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {

/**
 * Well-known resource monior names.
 * NOTE: New resource monitors should use the well known name: envoy.resource_monitors.name.
 */
class ResourceMonitorNameValues {
public:
  // Heap monitor with statically configured max.
  const std::string FixedHeap = "envoy.resource_monitors.fixed_heap";
};

typedef ConstSingleton<ResourceMonitorNameValues> ResourceMonitorNames;

} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
