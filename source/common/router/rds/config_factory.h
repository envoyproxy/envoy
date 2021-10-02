#pragma once

#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Router {
namespace Rds {

template <class RouteConfiguration, class Config> class ConfigFactory {
public:
  virtual ~ConfigFactory() = default;

  virtual std::shared_ptr<const Config> createConfig(const RouteConfiguration& rc) const PURE;
  virtual std::shared_ptr<const Config> createConfig() const PURE;
};

} // namespace Rds
} // namespace Router
} // namespace Envoy
