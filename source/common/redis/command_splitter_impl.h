#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/redis/command_splitter.h"
#include "envoy/redis/conn_pool.h"

#include "common/common/logger.h"
#include "common/common/to_lower_table.h"

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

class AllParamsToOneServerCommandHandler : public CommandHandler,
                                           CommandHandlerBase,
                                           Logger::Loggable<Logger::Id::redis> {
public:
  AllParamsToOneServerCommandHandler(ConnPool::Instance& conn_pool)
      : CommandHandlerBase(conn_pool) {}

  // Redis::CommandSplitter::CommandHandler
  SplitRequestPtr startRequest(const RespValue& request, SplitCallbacks& callbacks) override;

private:
  struct SplitRequestImpl : public SplitRequest, public ConnPool::PoolCallbacks {
    SplitRequestImpl(SplitCallbacks& callbacks) : callbacks_(callbacks) {}
    ~SplitRequestImpl();

    // Redis::CommandSplitter::SplitRequest
    void cancel() override;

    // Redis::ConnPool::PoolCallbacks
    void onResponse(RespValuePtr&& value) override;
    void onFailure() override;

    SplitCallbacks& callbacks_;
    ConnPool::PoolRequest* handle_{};
  };
};

class MGETCommandHandler : public CommandHandler,
                           CommandHandlerBase,
                           Logger::Loggable<Logger::Id::redis> {
public:
  MGETCommandHandler(ConnPool::Instance& conn_pool) : CommandHandlerBase(conn_pool) {}

  // Redis::CommandSplitter::CommandHandler
  SplitRequestPtr startRequest(const RespValue& request, SplitCallbacks& callbacks) override;

private:
  struct SplitRequestImpl : public SplitRequest {
    struct PendingRequest : public ConnPool::PoolCallbacks {
      PendingRequest(SplitRequestImpl& parent, uint32_t index) : parent_(parent), index_(index) {}

      // Redis::ConnPool::PoolCallbacks
      void onResponse(RespValuePtr&& value) override {
        parent_.onResponse(std::move(value), index_);
      }
      void onFailure() override { parent_.onFailure(index_); }

      SplitRequestImpl& parent_;
      const uint32_t index_;
      ConnPool::PoolRequest* handle_{};
    };

    SplitRequestImpl(SplitCallbacks& callbacks, uint32_t num_responses);
    ~SplitRequestImpl();

    void onResponse(RespValuePtr&& value, uint32_t index);
    void onFailure(uint32_t index);

    // Redis::CommandSplitter::SplitRequest
    void cancel() override;

    SplitCallbacks& callbacks_;
    RespValuePtr pending_response_;
    std::vector<PendingRequest> pending_requests_;
    uint32_t pending_responses_;
  };
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
  AllParamsToOneServerCommandHandler all_to_one_handler_;
  MGETCommandHandler mget_handler_;
  std::unordered_map<std::string, HandlerData> command_map_;
  InstanceStats stats_;
  ToLowerTable to_lower_table_;
};

} // CommandSplitter
} // Redis
