#include "extensions/filters/network/common/redis/fault_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

Fault::Fault(envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault base_fault)
    : commands_(buildCommands(base_fault)) {
  // // Get delay
  delay_ms_ = std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(base_fault, delay, 0));

  // Get fault type
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

  // Get the default value/runtime key
  if (base_fault.fault_enabled().has_default_value()) {
    if (base_fault.fault_enabled().default_value().denominator() ==
        envoy::type::v3::FractionalPercent::HUNDRED) {
      default_value_ = base_fault.fault_enabled().default_value().numerator();
    } else {
      auto denominator = ProtobufPercentHelper::fractionalPercentDenominatorToInt(
          base_fault.fault_enabled().default_value().denominator());
      default_value_ = (base_fault.fault_enabled().default_value().numerator() * 100) / denominator;
    }
  } else {
    default_value_ = 0;
  }
  runtime_key_ = base_fault.fault_enabled().runtime_key();
};

std::vector<std::string> Fault::buildCommands(
    envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_RedisFault base_fault) {
  std::vector<std::string> commands;
  for (auto command : base_fault.commands()) {
    commands.emplace_back(absl::AsciiStrToLower(command));
  }
  return commands;
}

const std::string FaultManagerImpl::ALL_KEY = "ALL_KEY";

FaultManagerImpl::FaultManagerImpl(
    Runtime::RandomGenerator& random, Runtime::Loader& runtime,
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
    auto fault_ptr = std::make_shared<Fault>(base_fault);
    if (fault_ptr->commands().size() > 0) {
      for (std::string command : fault_ptr->commands()) {
        fault_map[command].emplace_back(fault_ptr);
      }
    } else {
      // Generic "ALL" entry in map for faults that map to all keys; also add to each command
      fault_map[FaultManagerImpl::ALL_KEY].emplace_back(fault_ptr);
    }
  }

  // Add the ALL keys faults to each command too so that we can just query faults by command.
  // Get all ALL_KEY faults.
  FaultMap::iterator it_outer = fault_map.find(FaultManagerImpl::ALL_KEY);
  if (it_outer != fault_map.end()) {
    // For each ALL_KEY fault...
    for (auto fault_ptr : it_outer->second) {
      // Loop through all unique commands other than ALL_KEY and add the fault.
      FaultMap::iterator it_inner;
      for (it_inner = fault_map.begin(); it_inner != fault_map.end(); it_inner++) {
        std::string command = it_inner->first;
        if (command != FaultManagerImpl::ALL_KEY) {
          fault_map[command].push_back(fault_ptr);
        }
      }
    }
  }
  return fault_map;
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
//
// Note that we do not check to make sure the probabilities of faults are <= 100%!
absl::optional<std::pair<FaultType, std::chrono::milliseconds>>
FaultManagerImpl::getFaultForCommandInternal(std::string command) const {
  FaultMap::const_iterator it_outer = fault_map_.find(command);
  if (it_outer != fault_map_.end()) {
    auto random_number = random_.random() % 100;
    int amortized_fault = 0;

    for (auto fault_ptr : it_outer->second) {
      uint64_t fault_injection_percentage;
      if (fault_ptr->runtimeKey().has_value()) {
        fault_injection_percentage = runtime_.snapshot().getInteger(fault_ptr->runtimeKey().value(),
                                                                    fault_ptr->defaultValue());
      } else {
        fault_injection_percentage = fault_ptr->defaultValue();
      }

      if (random_number < (fault_injection_percentage + amortized_fault)) {
        return std::make_pair(fault_ptr->faultType(), fault_ptr->delayMs());
      } else {
        amortized_fault += fault_injection_percentage;
      }
    }
  }

  return absl::nullopt;
}

absl::optional<std::pair<FaultType, std::chrono::milliseconds>>
FaultManagerImpl::getFaultForCommand(std::string command) const {
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

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy