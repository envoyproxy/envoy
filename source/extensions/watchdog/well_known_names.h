#pragma once

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
/**
 * Well-known watchdog action names.
 * NOTE: New filters should use the well known name: envoy.watchdog.name
 */
class WatchdogActionNameValues {
public:
  // Profile Action
  const std::string ProfileAction = "envoy.watchdog.profile_action";
};

using WatchdogActionNames = ConstSingleton<WatchdogActionNameValues>;

} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
