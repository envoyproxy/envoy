#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"

#include "extensions/filters/network/common/redis/supported_commands.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace CommandSplitter {

Common::Redis::RespValuePtr Utility::makeError(const std::string& error) {
  Common::Redis::RespValuePtr response(new Common::Redis::RespValue());
  response->type(Common::Redis::RespType::Error);
  response->asString() = error;
  return response;
}

void SplitRequestBase::onWrongNumberOfArguments(SplitCallbacks& callbacks,
                                                const Common::Redis::RespValue& request) {
  callbacks.onResponse(Utility::makeError(
      fmt::format("wrong number of arguments for '{}' command", request.asArray()[0].asString())));
}

void SplitRequestBase::updateStats(const bool success) {
  if (success) {
    command_stats_.success_.inc();
  } else {
    command_stats_.error_.inc();
  }
  command_latency_ms_->complete();
}

SingleServerRequest::~SingleServerRequest() { ASSERT(!handle_); }

void SingleServerRequest::onResponse(Common::Redis::RespValuePtr&& response) {
  handle_ = nullptr;
  updateStats(true);
  callbacks_.onResponse(std::move(response));
}

void SingleServerRequest::onFailure() {
  handle_ = nullptr;
  updateStats(false);
  callbacks_.onResponse(Utility::makeError(Response::get().UpstreamFailure));
}

bool SingleServerRequest::onMovedRedirection(Common::Redis::RespValue& value) {
  if ((incoming_request_.get() == nullptr) || (value.type() != Common::Redis::RespType::Error)) {
    return false;
  }
  std::vector<absl::string_view> err = StringUtil::splitToken(value.asString(), " ", false);
  if (err.size() != 3) {
    return false;
  }

  const std::string host_address = std::string(err[2]); // ip:port
  handle_ = conn_pool_.redirectRequest(host_address, *incoming_request_, *this);
  return (handle_ != nullptr);
}

bool SingleServerRequest::onAskRedirection(Common::Redis::RespValue& value) {
  if ((incoming_request_.get() == nullptr) || (value.type() != Common::Redis::RespType::Error)) {
    return false;
  }
  std::vector<absl::string_view> err = StringUtil::splitToken(value.asString(), " ", false);
  if (err.size() != 3) {
    return false;
  }

  if (incoming_request_->asArray().size() > 0 &&
      incoming_request_->type() == Common::Redis::RespType::Array) {
    Common::Redis::RespValue asking_cmd;
    asking_cmd.type(Common::Redis::RespType::BulkString);
    asking_cmd.asString() = "asking";
    auto it = incoming_request_->asArray().begin();
    incoming_request_->asArray().insert(it, asking_cmd);
  } else {
    return false;
  }

  const std::string host_address = std::string(err[2]); // ip:port
  handle_ = conn_pool_.redirectRequest(host_address, *incoming_request_, *this);
  incoming_request_->asArray().erase(incoming_request_->asArray().begin());
  return (handle_ != nullptr);
}

void SingleServerRequest::cancel() {
  handle_->cancel();
  handle_ = nullptr;
}

SplitRequestPtr SimpleRequest::create(ConnPool::Instance& conn_pool,
                                      const Common::Redis::RespValue& incoming_request,
                                      SplitCallbacks& callbacks, CommandStats& command_stats,
                                      TimeSource& time_source) {
  std::unique_ptr<SimpleRequest> request_ptr{
      new SimpleRequest(callbacks, command_stats, conn_pool, time_source)};

  request_ptr->handle_ = conn_pool.makeRequest(incoming_request.asArray()[1].asString(),
                                               incoming_request, *request_ptr);
  if (!request_ptr->handle_) {
    request_ptr->callbacks_.onResponse(Utility::makeError(Response::get().NoUpstreamHost));
    return nullptr;
  }

  return std::move(request_ptr);
}

SplitRequestPtr SimpleRequest::create(ConnPool::Instance& conn_pool,
                                      Common::Redis::RespValuePtr&& incoming_request,
                                      SplitCallbacks& callbacks, CommandStats& command_stats,
                                      TimeSource& time_source) {
  const Common::Redis::RespValue* incoming_request_ptr = incoming_request.get();
  std::unique_ptr<SimpleRequest> request_ptr{new SimpleRequest(
      callbacks, command_stats, conn_pool, std::move(incoming_request), time_source)};

  request_ptr->handle_ = conn_pool.makeRequest(incoming_request_ptr->asArray()[1].asString(),
                                               *incoming_request_ptr, *request_ptr);
  if (!request_ptr->handle_) {
    request_ptr->callbacks_.onResponse(Utility::makeError(Response::get().NoUpstreamHost));
    return nullptr;
  }

  return std::move(request_ptr);
}

SplitRequestPtr EvalRequest::create(ConnPool::Instance& conn_pool,
                                    const Common::Redis::RespValue& incoming_request,
                                    SplitCallbacks& callbacks, CommandStats& command_stats,
                                    TimeSource& time_source) {

  // EVAL looks like: EVAL script numkeys key [key ...] arg [arg ...]
  // Ensure there are at least three args to the command or it cannot be hashed.
  if (incoming_request.asArray().size() < 4) {
    onWrongNumberOfArguments(callbacks, incoming_request);
    command_stats.error_.inc();
    return nullptr;
  }

  std::unique_ptr<EvalRequest> request_ptr{
      new EvalRequest(callbacks, command_stats, conn_pool, time_source)};
  request_ptr->handle_ = conn_pool.makeRequest(incoming_request.asArray()[3].asString(),
                                               incoming_request, *request_ptr);
  if (!request_ptr->handle_) {
    command_stats.error_.inc();
    request_ptr->callbacks_.onResponse(Utility::makeError(Response::get().NoUpstreamHost));
    return nullptr;
  }

  return std::move(request_ptr);
}

SplitRequestPtr EvalRequest::create(ConnPool::Instance& conn_pool,
                                    Common::Redis::RespValuePtr&& incoming_request,
                                    SplitCallbacks& callbacks, CommandStats& command_stats,
                                    TimeSource& time_source) {
  const Common::Redis::RespValue* incoming_request_ptr = incoming_request.get();

  // EVAL looks like: EVAL script numkeys key [key ...] arg [arg ...]
  // Ensure there are at least three args to the command or it cannot be hashed.
  if (incoming_request->asArray().size() < 4) {
    onWrongNumberOfArguments(callbacks, *incoming_request);
    command_stats.error_.inc();
    return nullptr;
  }

  std::unique_ptr<EvalRequest> request_ptr{new EvalRequest(
      callbacks, command_stats, conn_pool, std::move(incoming_request), time_source)};
  request_ptr->handle_ = conn_pool.makeRequest(incoming_request_ptr->asArray()[3].asString(),
                                               *incoming_request_ptr, *request_ptr);
  if (!request_ptr->handle_) {
    command_stats.error_.inc();
    request_ptr->callbacks_.onResponse(Utility::makeError(Response::get().NoUpstreamHost));
    return nullptr;
  }

  return std::move(request_ptr);
}

FragmentedRequest::~FragmentedRequest() {
#ifndef NDEBUG
  for (const PendingRequest& request : pending_requests_) {
    ASSERT(!request.handle_);
  }
#endif
}

void FragmentedRequest::cancel() {
  for (PendingRequest& request : pending_requests_) {
    if (request.handle_) {
      request.handle_->cancel();
      request.handle_ = nullptr;
    }
  }
}

void FragmentedRequest::onChildFailure(uint32_t index) {
  onChildResponse(Utility::makeError(Response::get().UpstreamFailure), index);
}

SplitRequestPtr MGETRequest::createCommon(std::unique_ptr<MGETRequest>&& request_ptr,
                                          ConnPool::Instance& conn_pool,
                                          const Common::Redis::RespValue& incoming_request) {
  request_ptr->num_pending_responses_ = incoming_request.asArray().size() - 1;
  request_ptr->pending_requests_.reserve(request_ptr->num_pending_responses_);

  request_ptr->pending_response_ = std::make_unique<Common::Redis::RespValue>();
  request_ptr->pending_response_->type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> responses(request_ptr->num_pending_responses_);
  request_ptr->pending_response_->asArray().swap(responses);

  std::vector<Common::Redis::RespValue> values(2);
  values[0].type(Common::Redis::RespType::BulkString);
  values[0].asString() = "get";
  values[1].type(Common::Redis::RespType::BulkString);
  Common::Redis::RespValue single_mget;
  single_mget.type(Common::Redis::RespType::Array);
  single_mget.asArray().swap(values);

  for (uint64_t i = 1; i < incoming_request.asArray().size(); i++) {
    request_ptr->pending_requests_.emplace_back(*request_ptr, i - 1);
    PendingRequest& pending_request = request_ptr->pending_requests_.back();

    single_mget.asArray()[1].asString() = incoming_request.asArray()[i].asString();
    ENVOY_LOG(debug, "redis: parallel get: '{}'", single_mget.toString());
    pending_request.handle_ = conn_pool.makeRequest(incoming_request.asArray()[i].asString(),
                                                    single_mget, pending_request);
    if (!pending_request.handle_) {
      pending_request.onResponse(Utility::makeError(Response::get().NoUpstreamHost));
    }
  }

  return request_ptr->num_pending_responses_ > 0 ? std::move(request_ptr) : nullptr;
}

SplitRequestPtr MGETRequest::create(ConnPool::Instance& conn_pool,
                                    const Common::Redis::RespValue& incoming_request,
                                    SplitCallbacks& callbacks, CommandStats& command_stats,
                                    TimeSource& time_source) {
  std::unique_ptr<MGETRequest> request_ptr{
      new MGETRequest(callbacks, command_stats, conn_pool, time_source)};

  return MGETRequest::createCommon(std::move(request_ptr), conn_pool, incoming_request);
}

SplitRequestPtr MGETRequest::create(ConnPool::Instance& conn_pool,
                                    Common::Redis::RespValuePtr&& incoming_request,
                                    SplitCallbacks& callbacks, CommandStats& command_stats,
                                    TimeSource& time_source) {
  std::unique_ptr<MGETRequest> request_ptr{new MGETRequest(
      callbacks, command_stats, conn_pool, std::move(incoming_request), time_source)};
  const Common::Redis::RespValue& incoming_value = *(request_ptr->incoming_request_);

  return MGETRequest::createCommon(std::move(request_ptr), conn_pool, incoming_value);
}

void MGETRequest::onChildResponse(Common::Redis::RespValuePtr&& value, uint32_t index) {
  pending_requests_[index].handle_ = nullptr;

  pending_response_->asArray()[index].type(value->type());
  switch (value->type()) {
  case Common::Redis::RespType::Array:
  case Common::Redis::RespType::Integer:
  case Common::Redis::RespType::SimpleString: {
    pending_response_->asArray()[index].type(Common::Redis::RespType::Error);
    pending_response_->asArray()[index].asString() = Response::get().UpstreamProtocolError;
    error_count_++;
    break;
  }
  case Common::Redis::RespType::Error: {
    error_count_++;
    FALLTHRU;
  }
  case Common::Redis::RespType::BulkString: {
    pending_response_->asArray()[index].asString().swap(value->asString());
    break;
  }
  case Common::Redis::RespType::Null:
    break;
  }

  ASSERT(num_pending_responses_ > 0);
  if (--num_pending_responses_ == 0) {
    updateStats(error_count_ == 0);
    ENVOY_LOG(debug, "redis: response: '{}'", pending_response_->toString());
    callbacks_.onResponse(std::move(pending_response_));
  }
}

bool MGETRequest::onChildMovedRedirection(Common::Redis::RespValue& value, uint32_t index) {
  std::vector<absl::string_view> err = StringUtil::splitToken(value.asString(), " ", false);
  if (err.size() != 3) {
    return false;
  }

  std::vector<Common::Redis::RespValue> values(2);
  values[0].type(Common::Redis::RespType::BulkString);
  values[0].asString() = "get";
  values[1].type(Common::Redis::RespType::BulkString);
  values[1].asString() = incoming_request_->asArray()[index + 1].asString();
  Common::Redis::RespValue single_mget;
  single_mget.type(Common::Redis::RespType::Array);
  single_mget.asArray().swap(values);

  this->pending_requests_[index].handle_ =
      conn_pool_.redirectRequest(std::string(err[2]), single_mget, this->pending_requests_[index]);
  return (this->pending_requests_[index].handle_ != nullptr);
}

bool MGETRequest::onChildAskRedirection(Common::Redis::RespValue& value, uint32_t index) {
  std::vector<absl::string_view> err = StringUtil::splitToken(value.asString(), " ", false);
  if (err.size() != 3) {
    return false;
  }

  std::vector<Common::Redis::RespValue> values(3);
  values[0].type(Common::Redis::RespType::BulkString);
  values[0].asString() = "asking";
  values[1].type(Common::Redis::RespType::BulkString);
  values[1].asString() = "get";
  values[2].type(Common::Redis::RespType::BulkString);
  values[2].asString() = incoming_request_->asArray()[index + 1].asString();
  Common::Redis::RespValue single_mget;
  single_mget.type(Common::Redis::RespType::Array);
  single_mget.asArray().swap(values);

  this->pending_requests_[index].handle_ =
      conn_pool_.redirectRequest(std::string(err[2]), single_mget, this->pending_requests_[index]);
  return (this->pending_requests_[index].handle_ != nullptr);
}

SplitRequestPtr MSETRequest::createCommon(std::unique_ptr<MSETRequest>&& request_ptr,
                                          ConnPool::Instance& conn_pool,
                                          const Common::Redis::RespValue& incoming_request) {
  request_ptr->num_pending_responses_ = (incoming_request.asArray().size() - 1) / 2;
  request_ptr->pending_requests_.reserve(request_ptr->num_pending_responses_);

  request_ptr->pending_response_ = std::make_unique<Common::Redis::RespValue>();
  request_ptr->pending_response_->type(Common::Redis::RespType::SimpleString);

  std::vector<Common::Redis::RespValue> values(3);
  values[0].type(Common::Redis::RespType::BulkString);
  values[0].asString() = "set";
  values[1].type(Common::Redis::RespType::BulkString);
  values[2].type(Common::Redis::RespType::BulkString);
  Common::Redis::RespValue single_mset;
  single_mset.type(Common::Redis::RespType::Array);
  single_mset.asArray().swap(values);

  uint64_t fragment_index = 0;
  for (uint64_t i = 1; i < incoming_request.asArray().size(); i += 2) {
    request_ptr->pending_requests_.emplace_back(*request_ptr, fragment_index++);
    PendingRequest& pending_request = request_ptr->pending_requests_.back();

    single_mset.asArray()[1].asString() = incoming_request.asArray()[i].asString();
    single_mset.asArray()[2].asString() = incoming_request.asArray()[i + 1].asString();

    ENVOY_LOG(debug, "redis: parallel set: '{}'", single_mset.toString());
    pending_request.handle_ = conn_pool.makeRequest(incoming_request.asArray()[i].asString(),
                                                    single_mset, pending_request);
    if (!pending_request.handle_) {
      pending_request.onResponse(Utility::makeError(Response::get().NoUpstreamHost));
    }
  }

  return request_ptr->num_pending_responses_ > 0 ? std::move(request_ptr) : nullptr;
}

SplitRequestPtr MSETRequest::create(ConnPool::Instance& conn_pool,
                                    const Common::Redis::RespValue& incoming_request,
                                    SplitCallbacks& callbacks, CommandStats& command_stats,
                                    TimeSource& time_source) {
  if ((incoming_request.asArray().size() - 1) % 2 != 0) {
    onWrongNumberOfArguments(callbacks, incoming_request);
    command_stats.error_.inc();
    return nullptr;
  }
  std::unique_ptr<MSETRequest> request_ptr{
      new MSETRequest(callbacks, command_stats, conn_pool, time_source)};

  return MSETRequest::createCommon(std::move(request_ptr), conn_pool, incoming_request);
}

SplitRequestPtr MSETRequest::create(ConnPool::Instance& conn_pool,
                                    Common::Redis::RespValuePtr&& incoming_request,
                                    SplitCallbacks& callbacks, CommandStats& command_stats,
                                    TimeSource& time_source) {
  if ((incoming_request->asArray().size() - 1) % 2 != 0) {
    onWrongNumberOfArguments(callbacks, *incoming_request);
    command_stats.error_.inc();
    return nullptr;
  }
  std::unique_ptr<MSETRequest> request_ptr{new MSETRequest(
      callbacks, command_stats, conn_pool, std::move(incoming_request), time_source)};
  const Common::Redis::RespValue& incoming_value = *(request_ptr->incoming_request_);

  return MSETRequest::createCommon(std::move(request_ptr), conn_pool, incoming_value);
}

void MSETRequest::onChildResponse(Common::Redis::RespValuePtr&& value, uint32_t index) {
  pending_requests_[index].handle_ = nullptr;

  switch (value->type()) {
  case Common::Redis::RespType::SimpleString: {
    if (value->asString() == Response::get().OK) {
      break;
    }
    FALLTHRU;
  }
  default: {
    error_count_++;
    break;
  }
  }

  ASSERT(num_pending_responses_ > 0);
  if (--num_pending_responses_ == 0) {
    updateStats(error_count_ == 0);
    if (error_count_ == 0) {
      pending_response_->asString() = Response::get().OK;
      callbacks_.onResponse(std::move(pending_response_));
    } else {
      callbacks_.onResponse(
          Utility::makeError(fmt::format("finished with {} error(s)", error_count_)));
    }
  }
}

bool MSETRequest::onChildMovedRedirection(Common::Redis::RespValue& value, uint32_t index) {
  std::vector<absl::string_view> err = StringUtil::splitToken(value.asString(), " ", false);
  if (err.size() != 3) {
    return false;
  }

  std::vector<Common::Redis::RespValue> values(3);
  values[0].type(Common::Redis::RespType::BulkString);
  values[0].asString() = "set";
  values[1].type(Common::Redis::RespType::BulkString);
  values[1].asString() = incoming_request_->asArray()[(index * 2) + 1].asString();
  values[2].type(Common::Redis::RespType::BulkString);
  values[2].asString() = incoming_request_->asArray()[(index * 2) + 2].asString();
  Common::Redis::RespValue single_mset;
  single_mset.type(Common::Redis::RespType::Array);
  single_mset.asArray().swap(values);

  this->pending_requests_[index].handle_ =
      conn_pool_.redirectRequest(std::string(err[2]), single_mset, this->pending_requests_[index]);
  return (this->pending_requests_[index].handle_ != nullptr);
}

bool MSETRequest::onChildAskRedirection(Common::Redis::RespValue& value, uint32_t index) {
  std::vector<absl::string_view> err = StringUtil::splitToken(value.asString(), " ", false);
  if (err.size() != 3) {
    return false;
  }

  std::vector<Common::Redis::RespValue> values(4);
  values[0].type(Common::Redis::RespType::BulkString);
  values[0].asString() = "asking";
  values[1].type(Common::Redis::RespType::BulkString);
  values[1].asString() = "set";
  values[2].type(Common::Redis::RespType::BulkString);
  values[2].asString() = incoming_request_->asArray()[(index * 2) + 1].asString();
  values[3].type(Common::Redis::RespType::BulkString);
  values[3].asString() = incoming_request_->asArray()[(index * 2) + 2].asString();
  Common::Redis::RespValue single_mset;
  single_mset.type(Common::Redis::RespType::Array);
  single_mset.asArray().swap(values);

  this->pending_requests_[index].handle_ =
      conn_pool_.redirectRequest(std::string(err[2]), single_mset, this->pending_requests_[index]);
  return (this->pending_requests_[index].handle_ != nullptr);
}

SplitRequestPtr
SplitKeysSumResultRequest::createCommon(std::unique_ptr<SplitKeysSumResultRequest>&& request_ptr,
                                        ConnPool::Instance& conn_pool,
                                        const Common::Redis::RespValue& incoming_request) {
  request_ptr->num_pending_responses_ = incoming_request.asArray().size() - 1;
  request_ptr->pending_requests_.reserve(request_ptr->num_pending_responses_);

  request_ptr->pending_response_ = std::make_unique<Common::Redis::RespValue>();
  request_ptr->pending_response_->type(Common::Redis::RespType::Integer);

  std::vector<Common::Redis::RespValue> values(2);
  values[0].type(Common::Redis::RespType::BulkString);
  values[0].asString() = incoming_request.asArray()[0].asString();
  values[1].type(Common::Redis::RespType::BulkString);
  Common::Redis::RespValue single_fragment;
  single_fragment.type(Common::Redis::RespType::Array);
  single_fragment.asArray().swap(values);

  for (uint64_t i = 1; i < incoming_request.asArray().size(); i++) {
    request_ptr->pending_requests_.emplace_back(*request_ptr, i - 1);
    PendingRequest& pending_request = request_ptr->pending_requests_.back();

    single_fragment.asArray()[1].asString() = incoming_request.asArray()[i].asString();
    ENVOY_LOG(debug, "redis: parallel {}: '{}'", incoming_request.asArray()[0].asString(),
              single_fragment.toString());
    pending_request.handle_ = conn_pool.makeRequest(incoming_request.asArray()[i].asString(),
                                                    single_fragment, pending_request);
    if (!pending_request.handle_) {
      pending_request.onResponse(Utility::makeError(Response::get().NoUpstreamHost));
    }
  }

  return request_ptr->num_pending_responses_ > 0 ? std::move(request_ptr) : nullptr;
}

SplitRequestPtr SplitKeysSumResultRequest::create(ConnPool::Instance& conn_pool,
                                                  const Common::Redis::RespValue& incoming_request,
                                                  SplitCallbacks& callbacks,
                                                  CommandStats& command_stats,
                                                  TimeSource& time_source) {
  std::unique_ptr<SplitKeysSumResultRequest> request_ptr{
      new SplitKeysSumResultRequest(callbacks, command_stats, conn_pool, time_source)};

  return SplitKeysSumResultRequest::createCommon(std::move(request_ptr), conn_pool,
                                                 incoming_request);
}

SplitRequestPtr SplitKeysSumResultRequest::create(ConnPool::Instance& conn_pool,
                                                  Common::Redis::RespValuePtr&& incoming_request,
                                                  SplitCallbacks& callbacks,
                                                  CommandStats& command_stats,
                                                  TimeSource& time_source) {
  std::unique_ptr<SplitKeysSumResultRequest> request_ptr{new SplitKeysSumResultRequest(
      callbacks, command_stats, conn_pool, std::move(incoming_request), time_source)};
  const Common::Redis::RespValue& incoming_value = *(request_ptr->incoming_request_);

  return SplitKeysSumResultRequest::createCommon(std::move(request_ptr), conn_pool, incoming_value);
}

void SplitKeysSumResultRequest::onChildResponse(Common::Redis::RespValuePtr&& value,
                                                uint32_t index) {
  pending_requests_[index].handle_ = nullptr;

  switch (value->type()) {
  case Common::Redis::RespType::Integer: {
    total_ += value->asInteger();
    break;
  }
  default: {
    error_count_++;
    break;
  }
  }

  ASSERT(num_pending_responses_ > 0);
  if (--num_pending_responses_ == 0) {
    updateStats(error_count_ == 0);
    if (error_count_ == 0) {
      pending_response_->asInteger() = total_;
      callbacks_.onResponse(std::move(pending_response_));
    } else {
      callbacks_.onResponse(
          Utility::makeError(fmt::format("finished with {} error(s)", error_count_)));
    }
  }
}

bool SplitKeysSumResultRequest::onChildMovedRedirection(Common::Redis::RespValue& value,
                                                        uint32_t index) {
  std::vector<absl::string_view> err = StringUtil::splitToken(value.asString(), " ", false);
  if (err.size() != 3) {
    return false;
  }

  std::vector<Common::Redis::RespValue> values(2);
  values[0].type(Common::Redis::RespType::BulkString);
  values[0].asString() = incoming_request_->asArray()[0].asString();
  values[1].type(Common::Redis::RespType::BulkString);
  values[1].asString() = incoming_request_->asArray()[index + 1].asString();
  Common::Redis::RespValue single_fragment;
  single_fragment.type(Common::Redis::RespType::Array);
  single_fragment.asArray().swap(values);

  this->pending_requests_[index].handle_ = conn_pool_.redirectRequest(
      std::string(err[2]), single_fragment, this->pending_requests_[index]);
  return (this->pending_requests_[index].handle_ != nullptr);
}

bool SplitKeysSumResultRequest::onChildAskRedirection(Common::Redis::RespValue& value,
                                                      uint32_t index) {
  std::vector<absl::string_view> err = StringUtil::splitToken(value.asString(), " ", false);
  if (err.size() != 3) {
    return false;
  }

  std::vector<Common::Redis::RespValue> values(3);
  values[0].type(Common::Redis::RespType::BulkString);
  values[0].asString() = "asking";
  values[1].type(Common::Redis::RespType::BulkString);
  values[1].asString() = incoming_request_->asArray()[0].asString();
  values[2].type(Common::Redis::RespType::BulkString);
  values[2].asString() = incoming_request_->asArray()[index + 1].asString();
  Common::Redis::RespValue single_fragment;
  single_fragment.type(Common::Redis::RespType::Array);
  single_fragment.asArray().swap(values);

  this->pending_requests_[index].handle_ = conn_pool_.redirectRequest(
      std::string(err[2]), single_fragment, this->pending_requests_[index]);
  return (this->pending_requests_[index].handle_ != nullptr);
}

InstanceImpl::InstanceImpl(ConnPool::InstancePtr&& conn_pool, Stats::Scope& scope,
                           const std::string& stat_prefix, TimeSource& time_source)
    : conn_pool_(std::move(conn_pool)), simple_command_handler_(*conn_pool_),
      eval_command_handler_(*conn_pool_), mget_handler_(*conn_pool_), mset_handler_(*conn_pool_),
      split_keys_sum_result_handler_(*conn_pool_),
      stats_{ALL_COMMAND_SPLITTER_STATS(POOL_COUNTER_PREFIX(scope, stat_prefix + "splitter."))},
      time_source_(time_source) {
  for (const std::string& command : Common::Redis::SupportedCommands::simpleCommands()) {
    addHandler(scope, stat_prefix, command, simple_command_handler_);
  }

  for (const std::string& command : Common::Redis::SupportedCommands::evalCommands()) {
    addHandler(scope, stat_prefix, command, eval_command_handler_);
  }

  for (const std::string& command :
       Common::Redis::SupportedCommands::hashMultipleSumResultCommands()) {
    addHandler(scope, stat_prefix, command, split_keys_sum_result_handler_);
  }

  addHandler(scope, stat_prefix, Common::Redis::SupportedCommands::mget(), mget_handler_);
  addHandler(scope, stat_prefix, Common::Redis::SupportedCommands::mset(), mset_handler_);
}

SplitRequestPtr InstanceImpl::makeRequest(const Common::Redis::RespValue& request,
                                          SplitCallbacks& callbacks) {
  if (request.type() != Common::Redis::RespType::Array) {
    onInvalidRequest(callbacks);
    return nullptr;
  }

  std::string to_lower_string(request.asArray()[0].asString());
  to_lower_table_.toLowerCase(to_lower_string);

  if (to_lower_string == Common::Redis::SupportedCommands::ping()) {
    // Respond to PING locally.
    Common::Redis::RespValuePtr pong(new Common::Redis::RespValue());
    pong->type(Common::Redis::RespType::SimpleString);
    pong->asString() = "PONG";
    callbacks.onResponse(std::move(pong));
    return nullptr;
  }

  if (request.asArray().size() < 2) {
    // Commands other than PING all have at least two arguments.
    onInvalidRequest(callbacks);
    return nullptr;
  }

  for (const Common::Redis::RespValue& value : request.asArray()) {
    if (value.type() != Common::Redis::RespType::BulkString) {
      onInvalidRequest(callbacks);
      return nullptr;
    }
  }

  auto handler = handler_lookup_table_.find(to_lower_string.c_str());
  if (handler == nullptr) {
    stats_.unsupported_command_.inc();
    callbacks.onResponse(Utility::makeError(
        fmt::format("unsupported command '{}'", request.asArray()[0].asString())));
    return nullptr;
  }
  ENVOY_LOG(debug, "redis: splitting '{}'", request.toString());
  handler->command_stats_.total_.inc();
  SplitRequestPtr request_ptr = handler->handler_.get().startRequest(
      request, callbacks, handler->command_stats_, time_source_);
  return request_ptr;
}

SplitRequestPtr InstanceImpl::makeRequest(Common::Redis::RespValuePtr&& request,
                                          SplitCallbacks& callbacks) {
  if (request->type() != Common::Redis::RespType::Array) {
    onInvalidRequest(callbacks);
    return nullptr;
  }

  std::string to_lower_string(request->asArray()[0].asString());
  to_lower_table_.toLowerCase(to_lower_string);

  if (to_lower_string == Common::Redis::SupportedCommands::ping()) {
    // Respond to PING locally.
    Common::Redis::RespValuePtr pong(new Common::Redis::RespValue());
    pong->type(Common::Redis::RespType::SimpleString);
    pong->asString() = "PONG";
    callbacks.onResponse(std::move(pong));
    return nullptr;
  }

  if (request->asArray().size() < 2) {
    // Commands other than PING all have at least two arguments.
    onInvalidRequest(callbacks);
    return nullptr;
  }

  for (const Common::Redis::RespValue& value : request->asArray()) {
    if (value.type() != Common::Redis::RespType::BulkString) {
      onInvalidRequest(callbacks);
      return nullptr;
    }
  }

  auto handler = handler_lookup_table_.find(to_lower_string.c_str());
  if (handler == nullptr) {
    stats_.unsupported_command_.inc();
    callbacks.onResponse(Utility::makeError(
        fmt::format("unsupported command '{}'", request->asArray()[0].asString())));
    return nullptr;
  }
  ENVOY_LOG(debug, "redis: splitting '{}'", request->toString());
  handler->command_stats_.total_.inc();
  SplitRequestPtr request_ptr = handler->handler_.get().startRequest(
      std::move(request), callbacks, handler->command_stats_, time_source_);
  return request_ptr;
}

void InstanceImpl::onInvalidRequest(SplitCallbacks& callbacks) {
  stats_.invalid_request_.inc();
  callbacks.onResponse(Utility::makeError(Response::get().InvalidRequest));
}

void InstanceImpl::addHandler(Stats::Scope& scope, const std::string& stat_prefix,
                              const std::string& name, CommandHandler& handler) {
  std::string to_lower_name(name);
  to_lower_table_.toLowerCase(to_lower_name);
  const std::string command_stat_prefix = fmt::format("{}command.{}.", stat_prefix, to_lower_name);
  handler_lookup_table_.add(
      to_lower_name.c_str(),
      std::make_shared<HandlerData>(HandlerData{
          CommandStats{ALL_COMMAND_STATS(POOL_COUNTER_PREFIX(scope, command_stat_prefix),
                                         POOL_HISTOGRAM_PREFIX(scope, command_stat_prefix))},
          handler}));
}

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
