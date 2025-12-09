#include "source/extensions/filters/network/redis_proxy/cluster_response_handler.h"

#include <cinttypes>

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/common/redis/utility.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter_impl.h"
#include "source/extensions/filters/network/redis_proxy/info_command_handler.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "fmt/format.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace CommandSplitter {

// Implementation of ClusterResponseHandlerFactory
std::unique_ptr<BaseClusterScopeResponseHandler>
ClusterResponseHandlerFactory::createHandler(const std::string& command_name,
                                             const std::string& subcommand, uint32_t shard_count) {
  ClusterScopeResponseHandlerType handler_type = getResponseHandlerType(command_name, subcommand);

  switch (handler_type) {
  case ClusterScopeResponseHandlerType::allresponses_mustbe_same:
    return std::make_unique<AllshardSameResponseHandler>(shard_count);
  case ClusterScopeResponseHandlerType::aggregate_all_responses:
    // Create specific aggregate handler based on command and subcommand
    return createAggregateHandler(command_name, subcommand, shard_count);
  default:
    return nullptr; // No handler for this command
  }
}

std::unique_ptr<BaseClusterScopeResponseHandler>
ClusterResponseHandlerFactory::createAggregateHandler(const std::string& command_name,
                                                      const std::string& subcommand,
                                                      uint32_t shard_count) {

  // SLOWLOG commands
  if (command_name == "slowlog") {
    if (subcommand == "len") {
      return std::make_unique<IntegerSumAggregateResponseHandler>(shard_count);
    } else if (subcommand == "get") {
      return std::make_unique<ArrayMergeAggregateResponseHandler>(shard_count);
    }
  }

  // CONFIG commands
  if (command_name == "config" && subcommand == "get") {
    return std::make_unique<ArrayMergeAggregateResponseHandler>(shard_count);
  }

  // INFO command
  if (command_name == "info") {
    return std::make_unique<InfoCmdAggregateResponseHandler>(shard_count, subcommand);
  }

  // KEYS command
  if (command_name == "keys") {
    return std::make_unique<ArrayMergeAggregateResponseHandler>(shard_count);
  }

  // ROLE command
  if (command_name == "role") {
    return std::make_unique<ArrayAppendAggregateResponseHandler>(shard_count);
  }

  // This should never be reached - all commands mapped to aggregate_all_responses
  // should be handled above
  ASSERT(false, fmt::format("Unhandled aggregate command: {}:{}", command_name, subcommand));
  return nullptr; // Unreachable, but needed for compilation
}

std::unique_ptr<BaseClusterScopeResponseHandler>
ClusterResponseHandlerFactory::createFromRequest(const Common::Redis::RespValue& request,
                                                 uint32_t shard_count) {
  if (request.type() != Common::Redis::RespType::Array || request.asArray().empty()) {
    return nullptr;
  }

  const std::string command_name = absl::AsciiStrToLower(request.asArray()[0].asString());
  std::string subcommand = "";

  if (request.asArray().size() > 1) {
    subcommand = absl::AsciiStrToLower(request.asArray()[1].asString());
  }

  return createHandler(command_name, subcommand, shard_count);
}

ClusterScopeResponseHandlerType
ClusterResponseHandlerFactory::getResponseHandlerType(const std::string& command_name,
                                                      const std::string& subcommand) {
  // Based on ClusterScopeCommands: script, flushall, flushdb, slowlog, config, info, keys, select
  // Note: randomkey and cluster are now handled by RandomShardRequest
  static const absl::flat_hash_map<std::string, ClusterScopeResponseHandlerType>
      command_to_handler_map = {
          // All shards must return same response
          {"script", ClusterScopeResponseHandlerType::allresponses_mustbe_same},
          {"flushall", ClusterScopeResponseHandlerType::allresponses_mustbe_same},
          {"flushdb", ClusterScopeResponseHandlerType::allresponses_mustbe_same},
          {"config:set", ClusterScopeResponseHandlerType::allresponses_mustbe_same},
          {"config:rewrite", ClusterScopeResponseHandlerType::allresponses_mustbe_same},
          {"config:resetstat", ClusterScopeResponseHandlerType::allresponses_mustbe_same},
          {"slowlog:reset", ClusterScopeResponseHandlerType::allresponses_mustbe_same},
          {"select", ClusterScopeResponseHandlerType::allresponses_mustbe_same},

          // Aggregate responses
          {"config:get", ClusterScopeResponseHandlerType::aggregate_all_responses},
          {"slowlog:get", ClusterScopeResponseHandlerType::aggregate_all_responses},
          {"slowlog:len", ClusterScopeResponseHandlerType::aggregate_all_responses},
          {"info", ClusterScopeResponseHandlerType::aggregate_all_responses},
          {"keys", ClusterScopeResponseHandlerType::aggregate_all_responses},
          {"role", ClusterScopeResponseHandlerType::aggregate_all_responses},
      };

  // First check with subcommand to see if there is a map entry
  if (!subcommand.empty()) {
    auto it = command_to_handler_map.find(command_name + ":" + subcommand);
    if (it != command_to_handler_map.end()) {
      return it->second;
    }
  }

  // Fallback to command only search for getting handler type
  auto it = command_to_handler_map.find(command_name);
  if (it != command_to_handler_map.end()) {
    return it->second;
  }
  // Default fallback - no handler found
  return ClusterScopeResponseHandlerType::response_handler_none;
}

// Implementation of BaseClusterScopeResponseHandler - Common functionality for all handlers
void BaseClusterScopeResponseHandler::handleResponse(Common::Redis::RespValuePtr&& value,
                                                     uint32_t shard_index,
                                                     ClusterScopeCmdRequest& request) {
  ENVOY_LOG(debug, "BaseClusterScopeResponseHandler: response received for shard index: '{}'",
            shard_index);

  // Store the response and update counters
  storeResponse(std::move(value), shard_index, request);

  // Early return if not all responses received yet
  ASSERT(num_pending_responses_ > 0);
  if (--num_pending_responses_ > 0) {
    return;
  }

  // Process all responses once we have them all - specific logic per handler type
  processAllResponses(request);
}

void BaseClusterScopeResponseHandler::storeResponse(Common::Redis::RespValuePtr&& value,
                                                    uint32_t shard_index,
                                                    ClusterScopeCmdRequest& request) {
  // Clean up the request handle - for ClusterScopeCmdRequest, shard_index == array index
  request.clearPendingHandle(shard_index);

  // Resize vector if needed to accommodate the shard_index
  if (shard_index >= pending_responses_.size()) {
    pending_responses_.resize(shard_index + 1);
  }

  // Track errors using handler's own state
  if (value && value->type() == Common::Redis::RespType::Error) {
    error_count_++;
  }

  // Store the response
  pending_responses_[shard_index] = std::move(value);
}

void BaseClusterScopeResponseHandler::handleErrorResponses(ClusterScopeCmdRequest& request) {
  request.updateRequestStats(false); // Update stats first for any error case

  // Find and return the first error response
  for (auto& resp : pending_responses_) {
    if (resp && resp->type() == Common::Redis::RespType::Error) {
      ENVOY_LOG(debug, "Error response received: '{}'", resp->toString());
      request.sendResponse(std::move(resp));
      return;
    }
  }
}

void BaseClusterScopeResponseHandler::sendErrorResponse(ClusterScopeCmdRequest& request,
                                                        const std::string& error_message) {
  ENVOY_LOG(error, "ClusterScopeResponseHandler error: {}", error_message);
  request.updateRequestStats(false);
  request.sendResponse(Common::Redis::Utility::makeError(error_message));
}

void BaseClusterScopeResponseHandler::sendSuccessResponse(ClusterScopeCmdRequest& request,
                                                          Common::Redis::RespValuePtr&& response) {
  ENVOY_LOG(debug, "Success response: {}", response->toString());
  request.updateRequestStats(true);
  request.sendResponse(std::move(response));
}

// Implementation of AllshardSameResponseHandler - Specific logic for same-response validation
void AllshardSameResponseHandler::processAllResponses(ClusterScopeCmdRequest& request) {

  ASSERT(!pending_responses_
              .empty()); // Empty responses should never happen for cluster scope commands

  // Handle error responses first
  if (error_count_ > 0) {
    handleErrorResponses(request);
    return;
  }

  // Validate all responses are the same
  if (!areAllResponsesSame()) {
    // Check if we have at least one response for logging
    if (!pending_responses_.empty() && pending_responses_[0]) {
      ENVOY_LOG(debug, "All responses not same: '{}'", pending_responses_[0]->toString());
    } else {
      ENVOY_LOG(debug, "All responses not same: responses are null or empty");
    }
    sendErrorResponse(request, "all responses not same");
    return;
  }

  // Success case - all responses are the same
  sendSuccessResponse(request, std::move(pending_responses_[0]));
}

bool AllshardSameResponseHandler::areAllResponsesSame() const {
  ASSERT(!pending_responses_.empty()); // Empty responses should never happen

  const Common::Redis::RespValue* first_response = pending_responses_.front().get();
  for (const auto& response : pending_responses_) {
    if (!response || !first_response || *(response.get()) != *first_response) {
      return false;
    }
  }
  return true;
}

// Implementation of BaseAggregateResponseHandler - Common aggregation pattern
void BaseAggregateResponseHandler::processAllResponses(ClusterScopeCmdRequest& request) {
  // Handle error responses first
  if (error_count_ > 0) {
    handleErrorResponses(request);
    return;
  }

  // Validate that we have responses - this should always be true for cluster scope commands
  ASSERT(!pending_responses_.empty());

  // Process aggregated responses - specific logic per handler type
  processAggregatedResponses(request);
}

// Implementation of IntegerSumAggregateResponseHandler
void IntegerSumAggregateResponseHandler::processAggregatedResponses(
    ClusterScopeCmdRequest& request) {
  int64_t sum = 0;
  for (const auto& resp : pending_responses_) {
    if (!resp) {
      sendErrorResponse(request, "null response received from shard");
      return;
    }

    if (resp->type() != Common::Redis::RespType::Integer) {
      sendErrorResponse(request, "non-integer response received from shard");
      return;
    }

    int64_t integerValue = resp->asInteger();
    if (integerValue < 0) {
      ENVOY_LOG(error, "Error: Negative integer value: {}", integerValue);
      sendErrorResponse(request, "negative value received from upstream");
      return;
    }
    sum += integerValue;
  }

  Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
  response->type(Common::Redis::RespType::Integer);
  response->asInteger() = sum;
  sendSuccessResponse(request, std::move(response));
}

// Implementation of ArrayMergeAggregateResponseHandler
void ArrayMergeAggregateResponseHandler::processAggregatedResponses(
    ClusterScopeCmdRequest& request) {
  Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
  response->type(Common::Redis::RespType::Array);

  for (const auto& resp : pending_responses_) {
    if (!resp) {
      sendErrorResponse(request, "null response received from shard");
      return;
    }

    if (resp->type() != Common::Redis::RespType::Array) {
      sendErrorResponse(request, "non-array response received from shard");
      return;
    }

    for (auto& elem : resp->asArray()) {
      response->asArray().emplace_back(std::move(elem));
    }
  }

  sendSuccessResponse(request, std::move(response));
}

// Implementation of ArrayAppendAggregateResponseHandler
void ArrayAppendAggregateResponseHandler::processAggregatedResponses(
    ClusterScopeCmdRequest& request) {
  Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
  response->type(Common::Redis::RespType::Array);

  for (const auto& resp : pending_responses_) {
    if (!resp) {
      sendErrorResponse(request, "null response received from shard");
      return;
    }

    if (resp->type() != Common::Redis::RespType::Array) {
      sendErrorResponse(request, "non-array response received from shard");
      return;
    }

    // Append the entire response as an element (preserves array structure)
    response->asArray().push_back(std::move(*resp));
  }

  sendSuccessResponse(request, std::move(response));
}

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
