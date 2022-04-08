#pragma once

#include <memory>

namespace Envoy {
namespace Rds {

/**
 * Base class for router configuration classes used with Rds.
 */
class Config {
public:
  virtual ~Config() = default;
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;

} // namespace Rds
} // namespace Envoy
