#include "extensions/filters/network/common/redis/fault_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

FaultManagerImpl::FaultManagerImpl(
    Runtime::RandomGenerator& random, Runtime::Loader& runtime,
    const Protobuf::RepeatedPtrField<
        ::envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault>
        faults)
    : random_(random), runtime_(runtime) {
  // Group faults by command
  for (auto fault : faults) {
    if (fault.commands_size() > 0) {
      for (auto& command : fault.commands()) {
        fault_map_.insert(FaultMapType::value_type(absl::AsciiStrToLower(command), fault));
      }
    } else {
      // Generic "ALL" entry in map for faults that map to all keys; also add to each command
      fault_map_.insert(FaultMapType::value_type(ALL_KEY, fault));
    }
  }

  // Add the ALL keys faults to each command too so that we can just query faults by command.
  std::pair<FaultMapType::iterator, FaultMapType::iterator> range = fault_map_.equal_range(ALL_KEY);
  for (FaultMapType::iterator it = range.first; it != range.second; ++it) {
    envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault fault = it->second;
    // Walk over all unique keys
    for (auto it = fault_map_.begin(), end = fault_map_.end(); it != end;
         it = fault_map_.upper_bound(it->first)) {
      std::string key = it->first;
      if (key != ALL_KEY) {
        fault_map_.insert(FaultMapType::value_type(key, fault));
      }
    }
  }
}

// Fault checking algorithm:
//
// For example, if we have an ERROR fault at 5% for all commands, and a DELAY fault at 10% for GET,
// if we receive a GET, we want 5% of GETs to get DELAY, and 5% to get ERROR. Thus, we need to
// amortize the percentages.
//
// 0. Get random number.
// 1. Get faults for given command.s
// 2. For each fault, calculate the amortized fault injection percentage.
absl::optional<std::pair<FaultType, std::chrono::milliseconds>>
FaultManagerImpl::getFaultForCommandInternal(std::string command) {
  auto random_number = random_.random();
  int amortized_fault = 0;

  std::pair<FaultMapType::iterator, FaultMapType::iterator> range = fault_map_.equal_range(command);
  for (FaultMapType::iterator it = range.first; it != range.second; ++it) {
    envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault fault = it->second;
    const uint64_t fault_injection_percentage = calculateFaultInjectionPercentage(fault);
    if (random_number % (100 - amortized_fault) < fault_injection_percentage) {
      return std::make_pair(getFaultType(fault), getFaultDelayMs(fault));
    } else {
      amortized_fault += fault_injection_percentage;
    }
  }

  return absl::nullopt;
}

absl::optional<std::pair<FaultType, std::chrono::milliseconds>>
FaultManagerImpl::getFaultForCommand(std::string command) {
  // Check if faults exist for given command; else use ALL_KEY and search for general faults.
  if (!fault_map_.empty()) {
    if (fault_map_.count(command) > 0) {
      return getFaultForCommandInternal(command);
    } else {
      return getFaultForCommandInternal(ALL_KEY);
    }
  }

  return absl::nullopt;
}

int FaultManagerImpl::numberOfFaults() { return fault_map_.size(); }

uint64_t FaultManagerImpl::calculateFaultInjectionPercentage(
    envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault& fault) {
  if (fault.has_fault_enabled()) {
    if (fault.fault_enabled().has_default_value()) {
      envoy::type::v3::FractionalPercent default_value = fault.fault_enabled().default_value();
      if (default_value.denominator() == envoy::type::v3::FractionalPercent::HUNDRED) {
        return runtime_.snapshot().getInteger(fault.fault_enabled().runtime_key(),
                                              default_value.numerator());
      } else {
        int denominator =
            ProtobufPercentHelper::fractionalPercentDenominatorToInt(default_value.denominator());
        int adjusted_numerator = (default_value.numerator() * 100) / denominator;
        return runtime_.snapshot().getInteger(fault.fault_enabled().runtime_key(),
                                              adjusted_numerator);
      }
    } else {
      // Default value is zero if not set
      return runtime_.snapshot().getInteger(fault.fault_enabled().runtime_key(), 0);
    }
  } else {
    // Default action- no fault injection
    return 0;
  }
}

std::chrono::milliseconds FaultManagerImpl::getFaultDelayMs(
    envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault& fault) {
  return std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(fault, delay, 0));
}

FaultType FaultManagerImpl::getFaultType(
    envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault& fault) {
  switch (fault.fault_type()) {
  case envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::RedisFault::DELAY:
    return FaultType::Delay;
  case envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::RedisFault::ERROR:
    return FaultType::Error;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
    break;
  }
}

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy