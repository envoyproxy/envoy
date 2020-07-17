#include "extensions/filters/network/common/redis/fault_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

struct FaultManagerKeyNamesValues {
  // The rbac filter rejected the request
  const std::string AllKey = "ALL_KEY";
};
using FaultManagerKeyNames = ConstSingleton<FaultManagerKeyNamesValues>;

FaultManagerImpl::FaultImpl::FaultImpl(
    envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault base_fault)
    : commands_(buildCommands(base_fault)) {
  delay_ms_ = std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(base_fault, delay, 0));

  switch (base_fault.fault_type()) {
  case envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::RedisFault::DELAY:
    fault_type_ = FaultType::Delay;
    break;
  case envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::RedisFault::ERROR:
    fault_type_ = FaultType::Error;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
    break;
  }

  default_value_ = base_fault.fault_enabled().default_value();
  runtime_key_ = base_fault.fault_enabled().runtime_key();
};

std::vector<std::string> FaultManagerImpl::FaultImpl::buildCommands(
    envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault base_fault) {
  std::vector<std::string> commands;
  for (const std::string& command : base_fault.commands()) {
    commands.emplace_back(absl::AsciiStrToLower(command));
  }
  return commands;
}

FaultManagerImpl::FaultManagerImpl(
    Random::RandomGenerator& random, Runtime::Loader& runtime,
    const Protobuf::RepeatedPtrField<
        ::envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault>
        faults)
    : fault_map_(buildFaultMap(faults)), random_(random), runtime_(runtime) {}

FaultMap FaultManagerImpl::buildFaultMap(
    const Protobuf::RepeatedPtrField<
        ::envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault>
        faults) {
  // Next, create the fault map that maps commands to pointers to Fault objects.
  // Group faults by command
  FaultMap fault_map;
  for (auto base_fault : faults) {
    auto fault_ptr = std::make_shared<FaultImpl>(base_fault);
    if (!fault_ptr->commands().empty()) {
      for (const std::string& command : fault_ptr->commands()) {
        fault_map[command].emplace_back(fault_ptr);
      }
    } else {
      // Generic "ALL" entry in map for faults that map to all keys; also add to each command
      fault_map[FaultManagerKeyNames::get().AllKey].emplace_back(fault_ptr);
    }
  }

  // Add the ALL keys faults to each command too so that we can just query faults by command.
  // Get all ALL_KEY faults.
  FaultMap::iterator it_outer = fault_map.find(FaultManagerKeyNames::get().AllKey);
  if (it_outer != fault_map.end()) {
    for (const FaultSharedPtr& fault_ptr : it_outer->second) {
      FaultMap::iterator it_inner;
      for (it_inner = fault_map.begin(); it_inner != fault_map.end(); it_inner++) {
        std::string command = it_inner->first;
        if (command != FaultManagerKeyNames::get().AllKey) {
          fault_map[command].push_back(fault_ptr);
        }
      }
    }
  }
  return fault_map;
}

uint64_t FaultManagerImpl::getIntegerNumeratorOfFractionalPercent(
    absl::string_view key, const envoy::type::v3::FractionalPercent& default_value) const {
  uint64_t numerator;
  if (default_value.denominator() == envoy::type::v3::FractionalPercent::HUNDRED) {
    numerator = default_value.numerator();
  } else {
    int denominator =
        ProtobufPercentHelper::fractionalPercentDenominatorToInt(default_value.denominator());
    numerator = (default_value.numerator() * 100) / denominator;
  }
  return runtime_.snapshot().getInteger(key, numerator);
}

// Fault checking algorithm:
//
// For example, if we have an ERROR fault at 5% for all commands, and a DELAY fault at 10% for GET,
// if we receive a GET, we want 5% of GETs to get DELAY, and 10% to get ERROR. Thus, we need to
// amortize the percentages.
//
// 0. Get random number.
// 1. Get faults for given command.
// 2. For each fault, calculate the amortized fault injection percentage.
//
// Note that we do not check to make sure the probabilities of faults are <= 100%!
const Fault* FaultManagerImpl::getFaultForCommandInternal(const std::string& command) const {
  FaultMap::const_iterator it_outer = fault_map_.find(command);
  if (it_outer != fault_map_.end()) {
    auto random_number = random_.random() % 100;
    int amortized_fault = 0;

    for (const FaultSharedPtr& fault_ptr : it_outer->second) {
      uint64_t fault_injection_percentage = getIntegerNumeratorOfFractionalPercent(
          fault_ptr->runtimeKey().value(), fault_ptr->defaultValue());
      if (random_number < (fault_injection_percentage + amortized_fault)) {
        return fault_ptr.get();
      } else {
        amortized_fault += fault_injection_percentage;
      }
    }
  }

  return nullptr;
}

const Fault* FaultManagerImpl::getFaultForCommand(const std::string& command) const {
  if (!fault_map_.empty()) {
    if (fault_map_.count(command) > 0) {
      return getFaultForCommandInternal(command);
    } else {
      return getFaultForCommandInternal(FaultManagerKeyNames::get().AllKey);
    }
  }

  return nullptr;
}

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy