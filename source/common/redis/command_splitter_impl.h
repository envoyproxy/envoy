#pragma once

#include "envoy/redis/command_splitter.h"
#include "envoy/redis/conn_pool.h"

#include "common/common/logger.h"

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

class InstanceImpl : public Instance, Logger::Loggable<Logger::Id::redis> {
public:
  InstanceImpl(ConnPool::InstancePtr&& conn_pool);

  // Redis::CommandSplitter::Instance
  SplitRequestPtr makeRequest(const RespValue& request, SplitCallbacks& callbacks) override;

private:
  ConnPool::InstancePtr conn_pool_;
  AllParamsToOneServerCommandHandler all_to_one_handler_;
  MGETCommandHandler mget_handler_;
  std::unordered_map<std::string, std::reference_wrapper<CommandHandler>> command_map_;
};

} // CommandSplitter
} // Redis
