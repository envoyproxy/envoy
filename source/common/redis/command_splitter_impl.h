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

class SimpleRequest : public SplitRequest, public ConnPool::PoolCallbacks {
public:
  SimpleRequest(ConnPool::Instance& conn_pool, const RespValue& request, SplitCallbacks& callbacks)
      : callbacks_(callbacks) {
    handle_ = conn_pool.makeRequest(request.asArray()[1].asString(), request, *this);

    if (!handle_) {
      callbacks_.onResponse(Utility::makeError("no upstream host"));
    }
  }

  static SplitRequestPtr create(ConnPool::Instance& conn_pool, const RespValue& incoming_request,
                                SplitCallbacks& callbacks) {
    SplitRequestPtr request_ptr =
        SplitRequestPtr{new SimpleRequest(conn_pool, incoming_request, callbacks)};
    return request_ptr;
  }

  void onResponse(RespValuePtr&& response) {
    handle_ = nullptr;
    callbacks_.onResponse(std::move(response));
  }

  void onFailure() {
    handle_ = nullptr;
    callbacks_.onResponse(Utility::makeError("upstream failure"));
  }

  void cancel() {
    handle_->cancel();
    handle_ = nullptr;
  }

private:
  SplitCallbacks& callbacks_;
  ConnPool::PoolRequest* handle_{};
};

class MGETRequest : public SplitRequest {
public:
  MGETRequest(ConnPool::Instance& conn_pool, const RespValue& request, SplitCallbacks& callbacks)
      : callbacks_(callbacks) {
    num_pending_responses_ = request.asArray().size() - 1;

    pending_response_.reset(new RespValue());
    pending_response_->type(RespType::Array);
    std::vector<RespValue> responses(num_pending_responses_);
    pending_response_->asArray().swap(responses);
    pending_requests_.reserve(num_pending_responses_);

    std::vector<RespValue> values(2);
    values[0].type(RespType::BulkString);
    values[0].asString() = "get";
    values[1].type(RespType::BulkString);
    RespValue single_mget;
    single_mget.type(RespType::Array);
    single_mget.asArray().swap(values);

    for (uint64_t i = 1; i < request.asArray().size(); i++) {
      pending_requests_.emplace_back(*this, i - 1);
      PendingRequest& pending_request = pending_requests_.back();

      single_mget.asArray()[1].asString() = request.asArray()[i].asString();
      pending_request.handle_ =
          conn_pool.makeRequest(request.asArray()[i].asString(), single_mget, pending_request);
      if (!pending_request.handle_) {
        pending_request.onResponse(Utility::makeError("no upstream host"));
      }
    }
  }

  static SplitRequestPtr create(ConnPool::Instance& conn_pool, const RespValue& incoming_request,
                                SplitCallbacks& callbacks) {
    SplitRequestPtr request_ptr =
        SplitRequestPtr{new MGETRequest(conn_pool, incoming_request, callbacks)};
    return request_ptr;
  }

  void onChildResponse(RespValuePtr&& value, uint32_t index) {
    pending_requests_[index].handle_ = nullptr;

    pending_response_->asArray()[index].type(value->type());
    switch (value->type()) {
    case RespType::Array:
    case RespType::Integer: {
      pending_response_->asArray()[index].type(RespType::Error);
      pending_response_->asArray()[index].asString() = "upstream protocol error";
      break;
    }
    case RespType::SimpleString:
    case RespType::BulkString:
    case RespType::Error: {
      pending_response_->asArray()[index].asString().swap(value->asString());
      break;
    }
    case RespType::Null:
      break;
    }

    // ASSERT(num_pending_responses_ > 0);
    if (--num_pending_responses_ == 0) {
      // ENVOY_LOG(debug, "redis: response: '{}'", pending_response_->toString());
      callbacks_.onResponse(std::move(pending_response_));
    }
  }

  void onChildFailure(uint32_t index) {
    onChildResponse(Utility::makeError("upstream failure"), index);
  }

  void cancel() override {
    for (PendingRequest& request : pending_requests_) {
      if (request.handle_) {
        request.handle_->cancel();
        request.handle_ = nullptr;
      }
    }
  }

private:
  struct PendingRequest : public ConnPool::PoolCallbacks {
    PendingRequest(MGETRequest& parent, uint32_t index) : parent_(parent), index_(index) {}

    void onResponse(RespValuePtr&& value) override {
      parent_.onChildResponse(std::move(value), index_);
    }
    void onFailure() override { parent_.onChildFailure(index_); }

    MGETRequest& parent_;
    const uint32_t index_;
    ConnPool::PoolRequest* handle_{};
  };

  SplitCallbacks& callbacks_;
  RespValuePtr pending_response_;
  std::vector<PendingRequest> pending_requests_;
  uint32_t num_pending_responses_;
};

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
  std::unordered_map<std::string, HandlerData> command_map_;
  InstanceStats stats_;
  const ToLowerTable to_lower_table_;
};

} // namespace CommandSplitter
} // namespace Redis
} // namespace Envoy
