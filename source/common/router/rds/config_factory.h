#pragma once

#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Router {
namespace Rds {

template <class RouteConfiguration, class Config> class ConfigFactory {
public:
  virtual ~ConfigFactory() = default;

  /**
   * Create a config object based on a route configuration.
   */
  virtual std::shared_ptr<const Config> createConfig(const RouteConfiguration& rc) const PURE;

  /**
   * Create a dummy config object without actual route configuration.
   * This object will be used before the first valid route configuration is fetched.
   */
  virtual std::shared_ptr<const Config> createConfig() const PURE;
};

} // namespace Rds
} // namespace Router
} // namespace Envoy
