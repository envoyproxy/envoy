#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/redis/command_splitter.h"
#include "envoy/redis/conn_pool.h"

#include "common/common/logger.h"
#include "common/common/to_lower_table.h"

namespace Envoy {
namespace Redis {
namespace CommandSplitter {

class Utility {
public:
  static RespValuePtr makeError(const std::string& error);
};

class CommandHandler {
public:
  virtual ~CommandHandler() {}

  virtual SplitRequestPtr startRequest(const RespValue& request, SplitCallbacks& callbacks) PURE;
};

class CommandHandlerBase {
protected:
  CommandHandlerBase(ConnPool::Instance& conn_pool) : conn_pool_(conn_pool) {}

  ConnPool::Instance& conn_pool_;
};

/**
 * SimpleRequest hashes the first argument as the key and sends the request to a single Redis.
 */
class SimpleRequest : public SplitRequest, public ConnPool::PoolCallbacks {
public:
  ~SimpleRequest();

  static SplitRequestPtr create(ConnPool::Instance& conn_pool, const RespValue& incoming_request,
                                SplitCallbacks& callbacks);

  // Redis::ConnPool::PoolCallbacks
  void onResponse(RespValuePtr&& response) override;
  void onFailure() override;

  // Redis::CommandSplitter::SplitRequest
  void cancel() override;

private:
  SimpleRequest(SplitCallbacks& callbacks) : callbacks_(callbacks) {}

  SplitCallbacks& callbacks_;
  ConnPool::PoolRequest* handle_{};
};

class FragmentedRequest : public SplitRequest {
public:
  ~FragmentedRequest();

  // Redis::CommandSplitter::SplitRequest
  void cancel() override;

protected:
  FragmentedRequest(SplitCallbacks& callbacks) : callbacks_(callbacks) {}

  struct PendingRequest : public ConnPool::PoolCallbacks {
    PendingRequest(FragmentedRequest& parent, uint32_t index) : parent_(parent), index_(index) {}

    // Redis::ConnPool::PoolCallbacks
    void onResponse(RespValuePtr&& value) override {
      parent_.onChildResponse(std::move(value), index_);
    }
    void onFailure() override { parent_.onChildFailure(index_); }

    FragmentedRequest& parent_;
    const uint32_t index_;
    ConnPool::PoolRequest* handle_{};
  };

  virtual void onChildResponse(RespValuePtr&& value, uint32_t index) PURE;
  void onChildFailure(uint32_t index);

  SplitCallbacks& callbacks_;
  RespValuePtr pending_response_;
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
  static SplitRequestPtr create(ConnPool::Instance& conn_pool, const RespValue& incoming_request,
                                SplitCallbacks& callbacks);

private:
  MGETRequest(SplitCallbacks& callbacks) : FragmentedRequest(callbacks) {}

  // Redis::CommandSplitter::FragmentedRequest
  void onChildResponse(RespValuePtr&& value, uint32_t index) override;
};

/**
 * SplitKeysSumResultRequest takes each key from the command and sends the same incoming command
 * with each key to the appropriate Redis server. The response from each Redis (which must be an
 * integer) is summed and returned to the user. If there is any error or failure in processing the
 * fragmented commands, an error will be returned.
 */
class SplitKeysSumResultRequest : public FragmentedRequest, Logger::Loggable<Logger::Id::redis> {
public:
  static SplitRequestPtr create(ConnPool::Instance& conn_pool, const RespValue& incoming_request,
                                SplitCallbacks& callbacks);

private:
  SplitKeysSumResultRequest(SplitCallbacks& callbacks) : FragmentedRequest(callbacks) {}

  // Redis::CommandSplitter::FragmentedRequest
  void onChildResponse(RespValuePtr&& value, uint32_t index) override;

  int64_t total_{0};
};

/**
 * MSETRequest takes each key and value pair from the command and sends a SET for each to the
 * appropriate Redis server. The response is an OK if all commands succeeded or an ERR if any
 * failed.
 */
class MSETRequest : public FragmentedRequest, Logger::Loggable<Logger::Id::redis> {
public:
  static SplitRequestPtr create(ConnPool::Instance& conn_pool, const RespValue& incoming_request,
                                SplitCallbacks& callbacks);

private:
  MSETRequest(SplitCallbacks& callbacks) : FragmentedRequest(callbacks) {}

  // Redis::CommandSplitter::FragmentedRequest
  void onChildResponse(RespValuePtr&& value, uint32_t index) override;
};

/**
 * CommandHandlerFactory is placed in the command lookup map for each supported command and is used
 * to create Request objects.
 */
template <class RequestClass>
class CommandHandlerFactory : public CommandHandler, CommandHandlerBase {
public:
  CommandHandlerFactory(ConnPool::Instance& conn_pool) : CommandHandlerBase(conn_pool) {}
  SplitRequestPtr startRequest(const RespValue& request, SplitCallbacks& callbacks) {
    return RequestClass::create(conn_pool_, request, callbacks);
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
               const std::string& stat_prefix);

  // Redis::CommandSplitter::Instance
  SplitRequestPtr makeRequest(const RespValue& request, SplitCallbacks& callbacks) override;

private:
  struct HandlerData {
    Stats::Counter& total_;
    std::reference_wrapper<CommandHandler> handler_;
  };

  void addHandler(Stats::Scope& scope, const std::string& stat_prefix, const std::string& name,
                  CommandHandler& handler);
  void onInvalidRequest(SplitCallbacks& callbacks);

  ConnPool::InstancePtr conn_pool_;
  CommandHandlerFactory<SimpleRequest> simple_command_handler_;
  CommandHandlerFactory<MGETRequest> mget_handler_;
  CommandHandlerFactory<MSETRequest> mset_handler_;
  CommandHandlerFactory<SplitKeysSumResultRequest> split_keys_sum_result_handler_;
  std::unordered_map<std::string, HandlerData> command_map_;
  InstanceStats stats_;
  const ToLowerTable to_lower_table_;
};

} // namespace CommandSplitter
} // namespace Redis
} // namespace Envoy
