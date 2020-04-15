#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/timespan.h"

#include "common/common/logger.h"
#include "common/common/utility.h"
#include "common/singleton/const_singleton.h"
#include "common/stats/timespan_impl.h"

#include "extensions/filters/network/common/redis/client_impl.h"
#include "extensions/filters/network/common/redis/fault.h"
#include "extensions/filters/network/common/redis/utility.h"
#include "extensions/filters/network/redis_proxy/command_splitter.h"
#include "extensions/filters/network/redis_proxy/conn_pool_impl.h"
#include "extensions/filters/network/redis_proxy/router.h"

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
  const std::string AuthRequiredError = "NOAUTH Authentication required.";
};

using Response = ConstSingleton<ResponseValues>;

/**
 * All command level stats. @see stats_macros.h
 */
#define ALL_COMMAND_STATS(COUNTER)                                                                 \
  COUNTER(total)                                                                                   \
  COUNTER(success)                                                                                 \
  COUNTER(error)                                                                                   \
  COUNTER(error_fault)                                                                             \
  COUNTER(delay_fault)

/**
 * Struct definition for all command stats. @see stats_macros.h
 */
struct CommandStats {
  ALL_COMMAND_STATS(GENERATE_COUNTER_STRUCT)
  Envoy::Stats::Histogram& latency_;
};

class CommandHandler {
public:
  virtual ~CommandHandler() = default;

  virtual SplitRequestPtr startRequest(Common::Redis::RespValuePtr&& request,
                                       SplitCallbacks& callbacks, CommandStats& command_stats,
                                       TimeSource& time_source) PURE;
};

struct HandlerData {
  CommandStats command_stats_;
  std::reference_wrapper<CommandHandler> handler_;
};

using HandlerDataPtr = std::shared_ptr<HandlerData>;

class CommandHandlerBase {
protected:
  CommandHandlerBase(Router& router) : router_(router) {}

  Router& router_;
};

class SplitRequestBase : public SplitRequest {
protected:
  static void onWrongNumberOfArguments(SplitCallbacks& callbacks,
                                       const Common::Redis::RespValue& request);
  void updateStats(const bool success);

  // To support delay faults, we allow faults to override the regular command latency
  // recording behavior.
  void delayLatencyMetric() { delay_command_latency_ = true; }
  void completeLatency() { command_latency_->complete(); }

  SplitRequestBase(CommandStats& command_stats, TimeSource& time_source)
      : command_stats_(command_stats) {
    command_latency_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
        command_stats_.latency_, time_source);
  }
  CommandStats& command_stats_;
  Stats::TimespanPtr command_latency_;
  bool delay_command_latency_;
};

/**
 * SingleServerRequest is a base class for commands that hash to a single backend.
 */
class SingleServerRequest : public SplitRequestBase, public ConnPool::PoolCallbacks {
public:
  ~SingleServerRequest() override;

  // ConnPool::PoolCallbacks
  void onResponse(Common::Redis::RespValuePtr&& response) override;
  void onFailure() override;

  // RedisProxy::CommandSplitter::SplitRequest
  void cancel() override;

protected:
  SingleServerRequest(SplitCallbacks& callbacks, CommandStats& command_stats,
                      TimeSource& time_source)
      : SplitRequestBase(command_stats, time_source), callbacks_(callbacks) {}

  SplitCallbacks& callbacks_;
  ConnPool::InstanceSharedPtr conn_pool_;
  Common::Redis::Client::PoolRequest* handle_{};
  Common::Redis::RespValuePtr incoming_request_;
};

/**
 * ErrorFaultRequest returns an error.
 */
class ErrorFaultRequest : public SingleServerRequest {
public:
  static SplitRequestPtr create(Router& router, Common::Redis::RespValuePtr&& incoming_request,
                                SplitCallbacks& callbacks, CommandStats& command_stats,
                                TimeSource& time_source);

private:
  ErrorFaultRequest(SplitCallbacks& callbacks, CommandStats& command_stats, TimeSource& time_source)
      : SingleServerRequest(callbacks, command_stats, time_source) {}

public:
  const static std::string FAULT_INJECTED_ERROR;
};

/**
 * DelayFaultRequest wraps a request- either a normal request or a fault- and delays it.
 */
class DelayFaultRequest : public SplitRequestBase, public SplitCallbacks {
public:
  static std::unique_ptr<DelayFaultRequest>
  create(SplitCallbacks& callbacks, CommandStats& command_stats, TimeSource& time_source,
         Event::Dispatcher& dispatcher, std::chrono::milliseconds delay);

  DelayFaultRequest(SplitCallbacks& callbacks, CommandStats& command_stats, TimeSource& time_source,
                    Event::Dispatcher& dispatcher, std::chrono::milliseconds delay)
      : SplitRequestBase(command_stats, time_source), callbacks_(callbacks),
        dispatcher_(dispatcher), delay_(delay) {}

  // SplitCallbacks
  bool connectionAllowed() override { return callbacks_.connectionAllowed(); }
  void onAuth(const std::string& password) override { callbacks_.onAuth(password); }
  void onResponse(Common::Redis::RespValuePtr&& response) override;

  // RedisProxy::CommandSplitter::SplitRequest
  void cancel() override;

  SplitRequestPtr wrapped_request_ptr_;

private:
  void onDelayResponse();

  SplitCallbacks& callbacks_;
  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds delay_;
  Event::TimerPtr delay_timer_;
  Common::Redis::RespValuePtr response_;
};

/**
 * SimpleRequest hashes the first argument as the key.
 */
class SimpleRequest : public SingleServerRequest {
public:
  static SplitRequestPtr create(Router& router, Common::Redis::RespValuePtr&& incoming_request,
                                SplitCallbacks& callbacks, CommandStats& command_stats,
                                TimeSource& time_source);

private:
  SimpleRequest(SplitCallbacks& callbacks, CommandStats& command_stats, TimeSource& time_source)
      : SingleServerRequest(callbacks, command_stats, time_source) {}
};

/**
 * EvalRequest hashes the fourth argument as the key.
 */
class EvalRequest : public SingleServerRequest {
public:
  static SplitRequestPtr create(Router& router, Common::Redis::RespValuePtr&& incoming_request,
                                SplitCallbacks& callbacks, CommandStats& command_stats,
                                TimeSource& time_source);

private:
  EvalRequest(SplitCallbacks& callbacks, CommandStats& command_stats, TimeSource& time_source)
      : SingleServerRequest(callbacks, command_stats, time_source) {}
};

/**
 * FragmentedRequest is a base class for requests that contains multiple keys. An individual request
 * is sent to the appropriate server for each key. The responses from all servers are combined and
 * returned to the client.
 */
class FragmentedRequest : public SplitRequestBase {
public:
  ~FragmentedRequest() override;

  // RedisProxy::CommandSplitter::SplitRequest
  void cancel() override;

protected:
  FragmentedRequest(SplitCallbacks& callbacks, CommandStats& command_stats, TimeSource& time_source)
      : SplitRequestBase(command_stats, time_source), callbacks_(callbacks) {}

  struct PendingRequest : public ConnPool::PoolCallbacks {
    PendingRequest(FragmentedRequest& parent, uint32_t index) : parent_(parent), index_(index) {}

    // ConnPool::PoolCallbacks
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
  static SplitRequestPtr create(Router& router, Common::Redis::RespValuePtr&& incoming_request,
                                SplitCallbacks& callbacks, CommandStats& command_stats,
                                TimeSource& time_source);

private:
  MGETRequest(SplitCallbacks& callbacks, CommandStats& command_stats, TimeSource& time_source)
      : FragmentedRequest(callbacks, command_stats, time_source) {}

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
  static SplitRequestPtr create(Router& router, Common::Redis::RespValuePtr&& incoming_request,
                                SplitCallbacks& callbacks, CommandStats& command_stats,
                                TimeSource& time_source);

private:
  SplitKeysSumResultRequest(SplitCallbacks& callbacks, CommandStats& command_stats,
                            TimeSource& time_source)
      : FragmentedRequest(callbacks, command_stats, time_source) {}

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
  static SplitRequestPtr create(Router& router, Common::Redis::RespValuePtr&& incoming_request,
                                SplitCallbacks& callbacks, CommandStats& command_stats,
                                TimeSource& time_source);

private:
  MSETRequest(SplitCallbacks& callbacks, CommandStats& command_stats, TimeSource& time_source)
      : FragmentedRequest(callbacks, command_stats, time_source) {}

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
  CommandHandlerFactory(Router& router) : CommandHandlerBase(router) {}
  SplitRequestPtr startRequest(Common::Redis::RespValuePtr&& request, SplitCallbacks& callbacks,
                               CommandStats& command_stats, TimeSource& time_source) override {
    return RequestClass::create(router_, std::move(request), callbacks, command_stats, time_source);
  }
};

/**
 * All splitter stats. @see stats_macros.h
 */
#define ALL_COMMAND_SPLITTER_STATS(COUNTER)                                                        \
  COUNTER(invalid_request)                                                                         \
  COUNTER(unsupported_command)

/**
 * Struct definition for all splitter stats. @see stats_macros.h
 */
struct InstanceStats {
  ALL_COMMAND_SPLITTER_STATS(GENERATE_COUNTER_STRUCT)
};

class InstanceImpl : public Instance, Logger::Loggable<Logger::Id::redis> {
public:
  InstanceImpl(RouterPtr&& router, Stats::Scope& scope, const std::string& stat_prefix,
               TimeSource& time_source, bool latency_in_micros, Event::Dispatcher& dispatcher,
               Common::Redis::FaultManagerPtr&& fault_manager);

  // RedisProxy::CommandSplitter::Instance
  SplitRequestPtr makeRequest(Common::Redis::RespValuePtr&& request,
                              SplitCallbacks& callbacks) override;

private:
  void addHandler(Stats::Scope& scope, const std::string& stat_prefix, const std::string& name,
                  bool latency_in_micros, CommandHandler& handler);
  void onInvalidRequest(SplitCallbacks& callbacks);

  RouterPtr router_;
  CommandHandlerFactory<SimpleRequest> simple_command_handler_;
  CommandHandlerFactory<EvalRequest> eval_command_handler_;
  CommandHandlerFactory<MGETRequest> mget_handler_;
  CommandHandlerFactory<MSETRequest> mset_handler_;
  CommandHandlerFactory<SplitKeysSumResultRequest> split_keys_sum_result_handler_;
  CommandHandlerFactory<ErrorFaultRequest> error_fault_handler_;
  TrieLookupTable<HandlerDataPtr> handler_lookup_table_;
  InstanceStats stats_;
  TimeSource& time_source_;
  Event::Dispatcher& dispatcher_;

  Common::Redis::FaultManagerPtr fault_manager_;

  const std::string ERROR_FAULT = "error_fault";
};

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
