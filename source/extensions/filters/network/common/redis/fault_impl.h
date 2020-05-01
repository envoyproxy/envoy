#pragma once

#include <map>
#include <string>
#include <vector>

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

class Fault {
public:
  Fault(envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault base_fault);

  FaultType faultType() const { return fault_type_; };
  std::chrono::milliseconds delayMs() const { return delay_ms_; };
  const std::vector<std::string> commands() const { return commands_; };
  uint64_t defaultValue() const { return default_value_; };
  absl::optional<std::string> runtimeKey() const { return runtime_key_; };

private:
  static std::vector<std::string> buildCommands(
      envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault base_fault);

  FaultType fault_type_;
  std::chrono::milliseconds delay_ms_;
  const std::vector<std::string> commands_;
  uint64_t default_value_;
  absl::optional<std::string> runtime_key_;
};

using FaultPtr = std::shared_ptr<Fault>;
using FaultMap = std::unordered_map<std::string, std::vector<FaultPtr>>;

/**
 * Fault management- creation, storage and retrieval. Faults are queried for by command,
 * so they are stored in an unordered map using the command as key. For faults that apply to
 * all commands, we use a special ALL_KEYS entry in the map.
 */
class FaultManagerImpl : public FaultManager {
public:
  FaultManagerImpl(Runtime::RandomGenerator& random, Runtime::Loader& runtime)
      : random_(random), runtime_(runtime){}; // For testing
  FaultManagerImpl(
      Runtime::RandomGenerator& random, Runtime::Loader& runtime,
      const Protobuf::RepeatedPtrField<
          ::envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault>
          base_faults);

  absl::optional<std::pair<FaultType, std::chrono::milliseconds>>
  getFaultForCommand(std::string command) const override;

  // Allow the unit test to have access to private members.
  friend class FaultTest;

private:
  static FaultMap
  buildFaultMap(const Protobuf::RepeatedPtrField<
                ::envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault>
                    faults);

  absl::optional<std::pair<FaultType, std::chrono::milliseconds>>
  getFaultForCommandInternal(std::string command) const;
  const FaultMap fault_map_;

public:
  static const std::string ALL_KEY;

protected:
  Runtime::RandomGenerator& random_;
  Runtime::Loader& runtime_;
};

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy