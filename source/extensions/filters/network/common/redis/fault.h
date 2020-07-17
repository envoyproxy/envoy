#pragma once

#include <chrono>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/type/v3/percent.pb.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

/**
 * Fault Type.
 */
enum class FaultType { Delay, Error };

class Fault {
public:
  virtual ~Fault() = default;

  virtual FaultType faultType() const PURE;
  virtual std::chrono::milliseconds delayMs() const PURE;
  virtual const std::vector<std::string> commands() const PURE;
  virtual envoy::type::v3::FractionalPercent defaultValue() const PURE;
  virtual absl::optional<std::string> runtimeKey() const PURE;
};

using FaultSharedPtr = std::shared_ptr<const Fault>;

class FaultManager {
public:
  virtual ~FaultManager() = default;

  /**
   * Get fault type and delay given a Redis command.
   * @param command supplies the Redis command string.
   */
  virtual const Fault* getFaultForCommand(const std::string& command) const PURE;
};

using FaultManagerPtr = std::unique_ptr<FaultManager>;

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy