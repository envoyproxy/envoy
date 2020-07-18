#pragma once

#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/upstream/upstream.h"

#include "common/protobuf/utility.h"
#include "common/singleton/const_singleton.h"

#include "extensions/filters/network/common/redis/fault.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

using FaultMap = absl::flat_hash_map<std::string, std::vector<FaultSharedPtr>>;

/**
 * Message returned for particular types of faults.
 */
struct FaultMessagesValues {
  const std::string Error = "Fault Injected: Error";
};
using FaultMessages = ConstSingleton<FaultMessagesValues>;

/**
 * Fault management- creation, storage and retrieval. Faults are queried for by command,
 * so they are stored in an unordered map using the command as key. For faults that apply to
 * all commands, we use a special ALL_KEYS entry in the map.
 */
class FaultManagerImpl : public FaultManager {
public:
  FaultManagerImpl(
      Random::RandomGenerator& random, Runtime::Loader& runtime,
      const Protobuf::RepeatedPtrField<
          ::envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault>
          base_faults);

  const Fault* getFaultForCommand(const std::string& command) const override;

  static FaultSharedPtr makeFaultForTest(Common::Redis::FaultType fault_type,
                                         std::chrono::milliseconds delay_ms) {
    envoy::type::v3::FractionalPercent default_value;
    default_value.set_numerator(100);
    default_value.set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
    FaultImpl fault =
        FaultImpl(fault_type, delay_ms, std::vector<std::string>(), default_value, "foo");
    return std::make_shared<FaultImpl>(fault);
  }

  // Allow the unit test to have access to private members.
  friend class FaultTest;

private:
  class FaultImpl : public Fault {
  public:
    FaultImpl(
        envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault base_fault);
    FaultImpl(FaultType fault_type, std::chrono::milliseconds delay_ms,
              const std::vector<std::string> commands,
              envoy::type::v3::FractionalPercent default_value,
              absl::optional<std::string> runtime_key)
        : fault_type_(fault_type), delay_ms_(delay_ms), commands_(commands),
          default_value_(default_value), runtime_key_(runtime_key) {} // For testing only

    FaultType faultType() const override { return fault_type_; };
    std::chrono::milliseconds delayMs() const override { return delay_ms_; };
    const std::vector<std::string> commands() const override { return commands_; };
    envoy::type::v3::FractionalPercent defaultValue() const override { return default_value_; };
    absl::optional<std::string> runtimeKey() const override { return runtime_key_; };

  private:
    static std::vector<std::string> buildCommands(
        envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault base_fault);

    FaultType fault_type_;
    std::chrono::milliseconds delay_ms_;
    const std::vector<std::string> commands_;
    envoy::type::v3::FractionalPercent default_value_;
    absl::optional<std::string> runtime_key_;
  };

  static FaultMap
  buildFaultMap(const Protobuf::RepeatedPtrField<
                ::envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault>
                    faults);

  uint64_t getIntegerNumeratorOfFractionalPercent(
      absl::string_view key, const envoy::type::v3::FractionalPercent& default_value) const;
  const Fault* getFaultForCommandInternal(const std::string& command) const;
  const FaultMap fault_map_;

protected:
  Random::RandomGenerator& random_;
  Runtime::Loader& runtime_;
};

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy