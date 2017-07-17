#include "common/redis/command_splitter_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/redis/supported_commands.h"

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

SplitRequestPtr SimpleRequest::create(ConnPool::Instance& conn_pool,
                                      const RespValue& incoming_request,
                                      SplitCallbacks& callbacks) {
  std::unique_ptr<SimpleRequest> request_ptr =
      std::unique_ptr<SimpleRequest>{new SimpleRequest(callbacks)};

  request_ptr->handle_ = conn_pool.makeRequest(incoming_request.asArray()[1].asString(),
                                               incoming_request, *request_ptr);
  if (!request_ptr->handle_) {
    request_ptr->callbacks_.onResponse(Utility::makeError("no upstream host"));
    return nullptr;
  }

  return std::move(request_ptr);
}

void SimpleRequest::onResponse(RespValuePtr&& response) {
  handle_ = nullptr;
  callbacks_.onResponse(std::move(response));
}

void SimpleRequest::onFailure() {
  handle_ = nullptr;
  callbacks_.onResponse(Utility::makeError("upstream failure"));
}

void SimpleRequest::cancel() {
  handle_->cancel();
  handle_ = nullptr;
}

SplitRequestPtr MGETRequest::create(ConnPool::Instance& conn_pool,
                                    const RespValue& incoming_request, SplitCallbacks& callbacks) {
  std::unique_ptr<MGETRequest> request_ptr =
      std::unique_ptr<MGETRequest>{new MGETRequest(callbacks)};

  request_ptr->num_pending_responses_ = incoming_request.asArray().size() - 1;

  request_ptr->pending_response_.reset(new RespValue());
  request_ptr->pending_response_->type(RespType::Array);
  std::vector<RespValue> responses(request_ptr->num_pending_responses_);
  request_ptr->pending_response_->asArray().swap(responses);
  request_ptr->pending_requests_.reserve(request_ptr->num_pending_responses_);

  std::vector<RespValue> values(2);
  values[0].type(RespType::BulkString);
  values[0].asString() = "get";
  values[1].type(RespType::BulkString);
  RespValue single_mget;
  single_mget.type(RespType::Array);
  single_mget.asArray().swap(values);

  for (uint64_t i = 1; i < incoming_request.asArray().size(); i++) {
    request_ptr->pending_requests_.emplace_back(*request_ptr, i - 1);
    PendingRequest& pending_request = request_ptr->pending_requests_.back();

    single_mget.asArray()[1].asString() = incoming_request.asArray()[i].asString();
    pending_request.handle_ = conn_pool.makeRequest(incoming_request.asArray()[i].asString(),
                                                    single_mget, pending_request);
    if (!pending_request.handle_) {
      pending_request.onResponse(Utility::makeError("no upstream host"));
    }
  }

  return request_ptr->num_pending_responses_ > 0 ? std::move(request_ptr) : nullptr;
}

void MGETRequest::onChildResponse(RespValuePtr&& value, uint32_t index) {
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

  ASSERT(num_pending_responses_ > 0);
  if (--num_pending_responses_ == 0) {
    // ENVOY_LOG(debug, "redis: response: '{}'", pending_response_->toString());
    callbacks_.onResponse(std::move(pending_response_));
  }
}

void MGETRequest::onChildFailure(uint32_t index) {
  onChildResponse(Utility::makeError("upstream failure"), index);
}

void MGETRequest::cancel() {
  for (PendingRequest& request : pending_requests_) {
    if (request.handle_) {
      request.handle_->cancel();
      request.handle_ = nullptr;
    }
  }
}

InstanceImpl::InstanceImpl(ConnPool::InstancePtr&& conn_pool, Stats::Scope& scope,
                           const std::string& stat_prefix)
    : conn_pool_(std::move(conn_pool)), simple_command_handler_(*conn_pool_),
      mget_handler_(*conn_pool_), stats_{ALL_COMMAND_SPLITTER_STATS(
                                      POOL_COUNTER_PREFIX(scope, stat_prefix + "splitter."))} {
  // TODO(mattklein123) PERF: Make this a trie (like in header_map_impl).
  for (const std::string& command : SupportedCommands::allToOneCommands()) {
    addHandler(scope, stat_prefix, command, simple_command_handler_);
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

} // namespace CommandSplitter
} // namespace Redis
} // namespace Envoy
