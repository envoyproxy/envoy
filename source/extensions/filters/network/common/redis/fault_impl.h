#pragma once

#include <map>
#include <string>

#include "envoy/api/api.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/upstream/upstream.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/network/common/redis/fault.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

/**
 * Fault management- creation, storage and retrieval. Faults are queried for by command,
 * so they are stored in a multimap using the command as key. For faults that apply to
 * all commands, we use a special ALL_KEYS entry in the map.
 */
class FaultManagerImpl : public FaultManager {
  typedef std::multimap<std::string,
                        envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault>
      FaultMapType;

public:
  FaultManagerImpl(Runtime::RandomGenerator& random, Runtime::Loader& runtime)
      : random_(random), runtime_(runtime){}; // For testing
  FaultManagerImpl(
      Runtime::RandomGenerator& random, Runtime::Loader& runtime,
      const Protobuf::RepeatedPtrField<
          ::envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault>
          faults);

  absl::optional<std::pair<FaultType, std::chrono::milliseconds>>
  getFaultForCommand(std::string command) override;
  int numberOfFaults() override;

  // Allow the unit test to have access to private members.
  friend class FaultTest;

private:
  absl::optional<std::pair<FaultType, std::chrono::milliseconds>>
  getFaultForCommandInternal(std::string command);

  uint64_t calculateFaultInjectionPercentage(
      envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault& fault);
  std::chrono::milliseconds getFaultDelayMs(
      envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault& fault);
  FaultType
  getFaultType(envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault& fault);

  const std::string ALL_KEY = "ALL_KEY";
  FaultMapType fault_map_;

protected:
  Runtime::RandomGenerator& random_;
  Runtime::Loader& runtime_;
};

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy