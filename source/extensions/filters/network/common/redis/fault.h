#pragma once

#include <string>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

/**
 * Fault Type.
 */
enum class FaultType { Delay, Error };

class FaultManager {
public:
  virtual ~FaultManager() = default;

  /**
   * Get fault type and delay given a Redis command.
   * @param command supplies the Redis command string.
   */
  virtual absl::optional<std::pair<FaultType, std::chrono::milliseconds>>
  getFaultForCommand(std::string command) const PURE;
};

using FaultManagerPtr = std::shared_ptr<FaultManager>;

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy