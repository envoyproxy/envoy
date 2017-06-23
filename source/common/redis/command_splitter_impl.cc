#include "common/redis/command_splitter_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/common/assert.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Redis {
namespace CommandSplitter {

RespValuePtr Utility::makeError(const std::string& error) {
  RespValuePtr response(new RespValue());
  response->type(RespType::Error);
  response->asString() = error;
  return response;
}

SplitRequestPtr AllParamsToOneServerCommandHandler::startRequest(const RespValue& request,
                                                                 SplitCallbacks& callbacks) {
  std::unique_ptr<SplitRequestImpl> request_handle(new SplitRequestImpl(callbacks));
  request_handle->handle_ =
      conn_pool_.makeRequest(request.asArray()[1].asString(), request, *request_handle);
  if (!request_handle->handle_) {
    callbacks.onResponse(Utility::makeError("no upstream host"));
    return nullptr;
  }

  return std::move(request_handle);
}

AllParamsToOneServerCommandHandler::SplitRequestImpl::~SplitRequestImpl() { ASSERT(!handle_); }

void AllParamsToOneServerCommandHandler::SplitRequestImpl::cancel() {
  handle_->cancel();
  handle_ = nullptr;
}

void AllParamsToOneServerCommandHandler::SplitRequestImpl::onResponse(RespValuePtr&& response) {
  handle_ = nullptr;
  ENVOY_LOG(debug, "redis: response: '{}'", response->toString());
  callbacks_.onResponse(std::move(response));
}

void AllParamsToOneServerCommandHandler::SplitRequestImpl::onFailure() {
  handle_ = nullptr;
  callbacks_.onResponse(Utility::makeError("upstream failure"));
}

SplitRequestPtr MGETCommandHandler::startRequest(const RespValue& request,
                                                 SplitCallbacks& callbacks) {
  std::unique_ptr<SplitRequestImpl> request_handle(
      new SplitRequestImpl(callbacks, request.asArray().size() - 1));

  // Create the get request that we will use for each split get below.
  std::vector<RespValue> values(2);
  values[0].type(RespType::BulkString);
  values[0].asString() = "get";
  values[1].type(RespType::BulkString);
  RespValue single_mget;
  single_mget.type(RespType::Array);
  single_mget.asArray().swap(values);

  for (uint64_t i = 1; i < request.asArray().size(); i++) {
    request_handle->pending_requests_.emplace_back(*request_handle, i - 1);
    SplitRequestImpl::PendingRequest& pending_request = request_handle->pending_requests_.back();

    single_mget.asArray()[1].asString() = request.asArray()[i].asString();
    ENVOY_LOG(debug, "redis: parallel get: '{}'", single_mget.toString());
    pending_request.handle_ =
        conn_pool_.makeRequest(request.asArray()[i].asString(), single_mget, pending_request);
    if (!pending_request.handle_) {
      pending_request.onResponse(Utility::makeError("no upstream host"));
    }
  }

  return request_handle->pending_responses_ > 0 ? std::move(request_handle) : nullptr;
}

MGETCommandHandler::SplitRequestImpl::SplitRequestImpl(SplitCallbacks& callbacks,
                                                       uint32_t num_responses)
    : callbacks_(callbacks), pending_responses_(num_responses) {
  pending_response_.reset(new RespValue());
  pending_response_->type(RespType::Array);
  std::vector<RespValue> responses(num_responses);
  pending_response_->asArray().swap(responses);
  pending_requests_.reserve(num_responses);
}

MGETCommandHandler::SplitRequestImpl::~SplitRequestImpl() {
#ifndef NDEBUG
  for (const PendingRequest& request : pending_requests_) {
    ASSERT(!request.handle_);
  }
#endif
}

void MGETCommandHandler::SplitRequestImpl::cancel() {
  for (PendingRequest& request : pending_requests_) {
    if (request.handle_) {
      request.handle_->cancel();
      request.handle_ = nullptr;
    }
  }
}

void MGETCommandHandler::SplitRequestImpl::onResponse(RespValuePtr&& value, uint32_t index) {
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

  ASSERT(pending_responses_ > 0);
  if (--pending_responses_ == 0) {
    ENVOY_LOG(debug, "redis: response: '{}'", pending_response_->toString());
    callbacks_.onResponse(std::move(pending_response_));
  }
}

void MGETCommandHandler::SplitRequestImpl::onFailure(uint32_t index) {
  onResponse(Utility::makeError("upstream failure"), index);
}

InstanceImpl::InstanceImpl(ConnPool::InstancePtr&& conn_pool, Stats::Scope& scope,
                           const std::string& stat_prefix)
    : conn_pool_(std::move(conn_pool)), all_to_one_handler_(*conn_pool_),
      mget_handler_(*conn_pool_),
      stats_{ALL_COMMAND_SPLITTER_STATS(POOL_COUNTER_PREFIX(scope, stat_prefix + "splitter."))} {
  // TODO(mattklein123) PERF: Make this a trie (like in header_map_impl).
  for (const std::string& command : Commands::allToOneCommands()) {
    addHandler(scope, stat_prefix, command, all_to_one_handler_);
  }

  // TODO(danielhochman): support for other multi-shard operations (del, mset)
  addHandler(scope, stat_prefix, "mget", mget_handler_);
}

SplitRequestPtr InstanceImpl::makeRequest(const RespValue& request, SplitCallbacks& callbacks) {
  if (request.type() != RespType::Array || request.asArray().size() < 2) {
    onInvalidRequest(callbacks);
    return nullptr;
  }

  for (const RespValue& value : request.asArray()) {
    if (value.type() != RespType::BulkString) {
      onInvalidRequest(callbacks);
      return nullptr;
    }
  }

  std::string to_lower_string(request.asArray()[0].asString());
  to_lower_table_.toLowerCase(to_lower_string);
  auto handler = command_map_.find(to_lower_string);
  if (handler == command_map_.end()) {
    stats_.unsupported_command_.inc();
    callbacks.onResponse(Utility::makeError(
        fmt::format("unsupported command '{}'", request.asArray()[0].asString())));
    return nullptr;
  }

  ENVOY_LOG(debug, "redis: splitting '{}'", request.toString());
  handler->second.total_.inc();
  return handler->second.handler_.get().startRequest(request, callbacks);
}

void InstanceImpl::onInvalidRequest(SplitCallbacks& callbacks) {
  stats_.invalid_request_.inc();
  callbacks.onResponse(Utility::makeError("invalid request"));
}

void InstanceImpl::addHandler(Stats::Scope& scope, const std::string& stat_prefix,
                              const std::string& name, CommandHandler& handler) {
  std::string to_lower_name(name);
  to_lower_table_.toLowerCase(to_lower_name);
  command_map_.emplace(
      to_lower_name,
      HandlerData{scope.counter(fmt::format("{}command.{}.total", stat_prefix, to_lower_name)),
                  handler});
}

} // CommandSplitter
} // Redis
} // Envoy
