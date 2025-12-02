#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/common/redis/client_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace CommandSplitter {

// Forward declaration to avoid circular dependency
class ClusterScopeCmdRequest;

/**
 * Enum defining the different response handler types for cluster scope commands
 */
enum class ClusterScopeResponseHandlerType {
  allresponses_mustbe_same,
  aggregate_all_responses,
  response_handler_none
};

/**
 * Base class for handling responses from cluster scope commands.
 * Implements the Template Method pattern where handleResponse() provides
 * the common algorithm and processAllResponses() allows customization.
 */
class BaseClusterScopeResponseHandler : public Logger::Loggable<Logger::Id::redis> {
public:
  virtual ~BaseClusterScopeResponseHandler() = default;

  /**
   * Handle a response from a shard
   * @param value the response value from upstream
   * @param shard_index the shard that sent this response (same as request index)
   * @param request the cluster scope request object (has all needed data)
   */
  void handleResponse(Common::Redis::RespValuePtr&& value, uint32_t shard_index,
                      ClusterScopeCmdRequest& request);

protected:
  // Response handler owns response tracking state
  uint32_t num_pending_responses_;
  uint32_t error_count_{0};
  std::vector<Common::Redis::RespValuePtr> pending_responses_;

  explicit BaseClusterScopeResponseHandler(uint32_t shard_count)
      : num_pending_responses_(shard_count), error_count_(0) {
    pending_responses_.reserve(shard_count);
  }

  // Template method pattern - derived classes implement specific processing
  virtual void processAllResponses(ClusterScopeCmdRequest& request) = 0;

  // Common helper methods available to all derived classes
  void storeResponse(Common::Redis::RespValuePtr&& value, uint32_t shard_index,
                     ClusterScopeCmdRequest& request);
  void sendErrorResponse(ClusterScopeCmdRequest& request, const std::string& error_message);
  void sendSuccessResponse(ClusterScopeCmdRequest& request, Common::Redis::RespValuePtr&& response);

  void handleErrorResponses(ClusterScopeCmdRequest& request);
};

/**
 * Response handler for commands where all shards must return the same response
 * Examples: CONFIG SET, FLUSHALL, SCRIPT FLUSH
 */
class AllshardSameResponseHandler : public BaseClusterScopeResponseHandler {
public:
  explicit AllshardSameResponseHandler(uint32_t shard_count)
      : BaseClusterScopeResponseHandler(shard_count) {}

protected:
  void processAllResponses(ClusterScopeCmdRequest& request) override;

private:
  bool areAllResponsesSame() const;
};

/**
 * Base class for aggregated response handlers
 */
class BaseAggregateResponseHandler : public BaseClusterScopeResponseHandler {
protected:
  explicit BaseAggregateResponseHandler(uint32_t shard_count)
      : BaseClusterScopeResponseHandler(shard_count) {}

  void processAllResponses(ClusterScopeCmdRequest& request) final;

  // Template method for specific aggregation logic
  virtual void processAggregatedResponses(ClusterScopeCmdRequest& request) = 0;
};

/**
 * Handler for integer sum aggregation (PUBSUB NUMPAT, SLOWLOG LEN)
 */
class IntegerSumAggregateResponseHandler : public BaseAggregateResponseHandler {
public:
  explicit IntegerSumAggregateResponseHandler(uint32_t shard_count)
      : BaseAggregateResponseHandler(shard_count) {}

private:
  void processAggregatedResponses(ClusterScopeCmdRequest& request) override;
};

/**
 * Handler for array merging (CONFIG GET, SLOWLOG GET)
 */
class ArrayMergeAggregateResponseHandler : public BaseAggregateResponseHandler {
public:
  explicit ArrayMergeAggregateResponseHandler(uint32_t shard_count)
      : BaseAggregateResponseHandler(shard_count) {}

private:
  void processAggregatedResponses(ClusterScopeCmdRequest& request) override;
};

/**
 * Factory class for creating appropriate response handlers
 */
class ClusterResponseHandlerFactory {
public:
  /**
   * Create a response handler based on the command and subcommand
   * @param command_name the Redis command (e.g., "config", "info")
   * @param subcommand the Redis subcommand if applicable (e.g., "get", "set")
   * @param shard_count the number of shards for memory pre-allocation
   * @return unique pointer to the appropriate response handler
   */
  static std::unique_ptr<BaseClusterScopeResponseHandler>
  createHandler(const std::string& command_name, const std::string& subcommand,
                uint32_t shard_count);

  /**
   * Create a response handler from a Redis request
   * @param request the incoming Redis request
   * @param shard_count the number of shards for memory pre-allocation
   * @return unique pointer to the appropriate response handler
   */
  static std::unique_ptr<BaseClusterScopeResponseHandler>
  createFromRequest(const Common::Redis::RespValue& request, uint32_t shard_count);

private:
  /**
   * Create a specific aggregate response handler based on command and subcommand
   * @param command_name the Redis command
   * @param subcommand the Redis subcommand if applicable
   * @param shard_count the number of shards for memory pre-allocation
   * @return unique pointer to the appropriate aggregate response handler
   */
  static std::unique_ptr<BaseClusterScopeResponseHandler>
  createAggregateHandler(const std::string& command_name, const std::string& subcommand,
                         uint32_t shard_count);

  /**
   * Get the response handler type for a given command and subcommand
   * @param command_name the Redis command
   * @param subcommand the Redis subcommand if applicable
   * @return the appropriate response handler type
   */
  static ClusterScopeResponseHandlerType getResponseHandlerType(const std::string& command_name,
                                                                const std::string& subcommand = "");
};

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
