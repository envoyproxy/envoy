#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/timespan.h"

#include "common/common/logger.h"
#include "common/common/to_lower_table.h"
#include "common/common/utility.h"
#include "common/singleton/const_singleton.h"

#include "extensions/filters/network/common/redis/client_impl.h"
#include "extensions/filters/network/redis_proxy/command_splitter.h"
#include "extensions/filters/network/redis_proxy/conn_pool.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace CommandSplitter {

struct ResponseValues {
  const std::string OK = "OK";
  const std::string InvalidRequest = "invalid request";
  const std::string NoUpstreamHost = "no upstream host";
  const std::string UpstreamFailure = "upstream failure";
  const std::string UpstreamProtocolError = "upstream protocol error";
};

typedef ConstSingleton<ResponseValues> Response;

class Utility {
public:
  static Common::Redis::RespValuePtr makeError(const std::string& error);
};

/**
 * All command level stats. @see stats_macros.h
 */
// clang-format off
#define ALL_COMMAND_STATS(COUNTER, HISTOGRAM)                                                      \
  COUNTER(total)                                                                                   \
  COUNTER(success)                                                                                 \
  COUNTER(error)                                                                                   \
  HISTOGRAM(latency)
// clang-format on

/**
 * Struct definition for all command stats. @see stats_macros.h
 */
struct CommandStats {
  ALL_COMMAND_STATS(GENERATE_COUNTER_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

class CommandHandler {
public:
  virtual ~CommandHandler() {}

  virtual SplitRequestPtr startRequest(const Common::Redis::RespValue& request,
                                       SplitCallbacks& callbacks, CommandStats& command_stats,
                                       TimeSource& time_source, bool latency_in_micros) PURE;
};

class CommandHandlerBase {
protected:
  CommandHandlerBase(ConnPool::Instance& conn_pool) : conn_pool_(conn_pool) {}

  ConnPool::Instance& conn_pool_;
};

class SplitRequestBase : public SplitRequest {
protected:
  static void onWrongNumberOfArguments(SplitCallbacks& callbacks,
                                       const Common::Redis::RespValue& request);
  void updateStats(const bool success);

  SplitRequestBase(CommandStats& command_stats, TimeSource& time_source, bool latency_in_micros)
      : command_stats_(command_stats) {
    if (latency_in_micros) {
      command_latency_ = std::make_unique<Stats::TimespanWithUnit<std::chrono::microseconds>>(
          command_stats_.latency_, time_source);
    } else {
      command_latency_ = std::make_unique<Stats::TimespanWithUnit<std::chrono::milliseconds>>(
          command_stats_.latency_, time_source);
    }
  }
  CommandStats& command_stats_;
  Stats::CompletableTimespanPtr command_latency_;
};

/**
 * SingleServerRequest is a base class for commands that hash to a single backend.
 */
class SingleServerRequest : public SplitRequestBase, public Common::Redis::Client::PoolCallbacks {
public:
  ~SingleServerRequest();

  // Common::Redis::Client::PoolCallbacks
  void onResponse(Common::Redis::RespValuePtr&& response) override;
  void onFailure() override;

  // RedisProxy::CommandSplitter::SplitRequest
  void cancel() override;

protected:
  SingleServerRequest(SplitCallbacks& callbacks, CommandStats& command_stats,
                      TimeSource& time_source, bool latency_in_micros)
      : SplitRequestBase(command_stats, time_source, latency_in_micros), callbacks_(callbacks) {}

  SplitCallbacks& callbacks_;
  Common::Redis::Client::PoolRequest* handle_{};
};

/**
 * SimpleRequest hashes the first argument as the key.
 */
class SimpleRequest : public SingleServerRequest {
public:
  static SplitRequestPtr create(ConnPool::Instance& conn_pool,
                                const Common::Redis::RespValue& incoming_request,
                                SplitCallbacks& callbacks, CommandStats& command_stats,
                                TimeSource& time_source, bool latency_in_micros);

private:
  SimpleRequest(SplitCallbacks& callbacks, CommandStats& command_stats, TimeSource& time_source,
                bool latency_in_micros)
      : SingleServerRequest(callbacks, command_stats, time_source, latency_in_micros) {}
};

/**
 * EvalRequest hashes the fourth argument as the key.
 */
class EvalRequest : public SingleServerRequest {
public:
  static SplitRequestPtr create(ConnPool::Instance& conn_pool,
                                const Common::Redis::RespValue& incoming_request,
                                SplitCallbacks& callbacks, CommandStats& command_stats,
                                TimeSource& time_source, bool latency_in_micros);

private:
  EvalRequest(SplitCallbacks& callbacks, CommandStats& command_stats, TimeSource& time_source,
              bool latency_in_micros)
      : SingleServerRequest(callbacks, command_stats, time_source, latency_in_micros) {}
};

/**
 * FragmentedRequest is a base class for requests that contains multiple keys. An individual request
 * is sent to the appropriate server for each key. The responses from all servers are combined and
 * returned to the client.
 */
class FragmentedRequest : public SplitRequestBase {
public:
  ~FragmentedRequest();

  // RedisProxy::CommandSplitter::SplitRequest
  void cancel() override;

protected:
  FragmentedRequest(SplitCallbacks& callbacks, CommandStats& command_stats, TimeSource& time_source,
                    bool latency_in_micros)
      : SplitRequestBase(command_stats, time_source, latency_in_micros), callbacks_(callbacks) {}

  struct PendingRequest : public Common::Redis::Client::PoolCallbacks {
    PendingRequest(FragmentedRequest& parent, uint32_t index) : parent_(parent), index_(index) {}

    // Common::Redis::Client::PoolCallbacks
    void onResponse(Common::Redis::RespValuePtr&& value) override {
      parent_.onChildResponse(std::move(value), index_);
    }
    void onFailure() override { parent_.onChildFailure(index_); }

    FragmentedRequest& parent_;
    const uint32_t index_;
    Common::Redis::Client::PoolRequest* handle_{};
  };

  virtual void onChildResponse(Common::Redis::RespValuePtr&& value, uint32_t index) PURE;
  void onChildFailure(uint32_t index);

  SplitCallbacks& callbacks_;
  Common::Redis::RespValuePtr pending_response_;
  std::vector<PendingRequest> pending_requests_;
  uint32_t num_pending_responses_;
  uint32_t error_count_{0};
};

/**
 * MGETRequest takes each key from the command and sends a GET for each to the appropriate Redis
 * server. The response contains the result from each command.
 */
class MGETRequest : public FragmentedRequest, Logger::Loggable<Logger::Id::redis> {
public:
  static SplitRequestPtr create(ConnPool::Instance& conn_pool,
                                const Common::Redis::RespValue& incoming_request,
                                SplitCallbacks& callbacks, CommandStats& command_stats,
                                TimeSource& time_source, bool latency_in_micros);

private:
  MGETRequest(SplitCallbacks& callbacks, CommandStats& command_stats, TimeSource& time_source,
              bool latency_in_micros)
      : FragmentedRequest(callbacks, command_stats, time_source, latency_in_micros) {}

  // RedisProxy::CommandSplitter::FragmentedRequest
  void onChildResponse(Common::Redis::RespValuePtr&& value, uint32_t index) override;
};

/**
 * SplitKeysSumResultRequest takes each key from the command and sends the same incoming command
 * with each key to the appropriate Redis server. The response from each Redis (which must be an
 * integer) is summed and returned to the user. If there is any error or failure in processing the
 * fragmented commands, an error will be returned.
 */
class SplitKeysSumResultRequest : public FragmentedRequest, Logger::Loggable<Logger::Id::redis> {
public:
  static SplitRequestPtr create(ConnPool::Instance& conn_pool,
                                const Common::Redis::RespValue& incoming_request,
                                SplitCallbacks& callbacks, CommandStats& command_stats,
                                TimeSource& time_source, bool latency_in_micros);

private:
  SplitKeysSumResultRequest(SplitCallbacks& callbacks, CommandStats& command_stats,
                            TimeSource& time_source, bool latency_in_micros)
      : FragmentedRequest(callbacks, command_stats, time_source, latency_in_micros) {}

  // RedisProxy::CommandSplitter::FragmentedRequest
  void onChildResponse(Common::Redis::RespValuePtr&& value, uint32_t index) override;

  int64_t total_{0};
};

/**
 * MSETRequest takes each key and value pair from the command and sends a SET for each to the
 * appropriate Redis server. The response is an OK if all commands succeeded or an ERR if any
 * failed.
 */
class MSETRequest : public FragmentedRequest, Logger::Loggable<Logger::Id::redis> {
public:
  static SplitRequestPtr create(ConnPool::Instance& conn_pool,
                                const Common::Redis::RespValue& incoming_request,
                                SplitCallbacks& callbacks, CommandStats& command_stats,
                                TimeSource& time_source, bool latency_in_micros);

private:
  MSETRequest(SplitCallbacks& callbacks, CommandStats& command_stats, TimeSource& time_source,
              bool latency_in_micros)
      : FragmentedRequest(callbacks, command_stats, time_source, latency_in_micros) {}

  // RedisProxy::CommandSplitter::FragmentedRequest
  void onChildResponse(Common::Redis::RespValuePtr&& value, uint32_t index) override;
};

/**
 * CommandHandlerFactory is placed in the command lookup map for each supported command and is used
 * to create Request objects.
 */
template <class RequestClass>
class CommandHandlerFactory : public CommandHandler, CommandHandlerBase {
public:
  CommandHandlerFactory(ConnPool::Instance& conn_pool) : CommandHandlerBase(conn_pool) {}
  SplitRequestPtr startRequest(const Common::Redis::RespValue& request, SplitCallbacks& callbacks,
                               CommandStats& command_stats, TimeSource& time_source,
                               bool latency_in_micros) {
    return RequestClass::create(conn_pool_, request, callbacks, command_stats, time_source,
                                latency_in_micros);
  }
};

/**
 * All splitter stats. @see stats_macros.h
 */
// clang-format off
#define ALL_COMMAND_SPLITTER_STATS(COUNTER)                                                        \
  COUNTER(invalid_request)                                                                         \
  COUNTER(unsupported_command)
// clang-format on

/**
 * Struct definition for all splitter stats. @see stats_macros.h
 */
struct InstanceStats {
  ALL_COMMAND_SPLITTER_STATS(GENERATE_COUNTER_STRUCT)
};

class InstanceImpl : public Instance, Logger::Loggable<Logger::Id::redis> {
public:
  InstanceImpl(ConnPool::InstancePtr&& conn_pool, Stats::Scope& scope,
               const std::string& stat_prefix, TimeSource& time_source, bool latency_in_micros);

  // RedisProxy::CommandSplitter::Instance
  SplitRequestPtr makeRequest(const Common::Redis::RespValue& request,
                              SplitCallbacks& callbacks) override;

private:
  struct HandlerData {
    CommandStats command_stats_;
    std::reference_wrapper<CommandHandler> handler_;
  };

  typedef std::shared_ptr<HandlerData> HandlerDataPtr;

  void addHandler(Stats::Scope& scope, const std::string& stat_prefix, const std::string& name,
                  CommandHandler& handler);
  void onInvalidRequest(SplitCallbacks& callbacks);

  ConnPool::InstancePtr conn_pool_;
  CommandHandlerFactory<SimpleRequest> simple_command_handler_;
  CommandHandlerFactory<EvalRequest> eval_command_handler_;
  CommandHandlerFactory<MGETRequest> mget_handler_;
  CommandHandlerFactory<MSETRequest> mset_handler_;
  CommandHandlerFactory<SplitKeysSumResultRequest> split_keys_sum_result_handler_;
  TrieLookupTable<HandlerDataPtr> handler_lookup_table_;
  InstanceStats stats_;
  const ToLowerTable to_lower_table_;
  const bool latency_in_micros_;
  TimeSource& time_source_;
};

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
