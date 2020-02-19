#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"

#include "extensions/filters/network/common/redis/supported_commands.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace CommandSplitter {
namespace {

// null_pool_callbacks is used for requests that must be filtered and not redirected such as
// "asking".
ConnPool::DoNothingPoolCallbacks null_pool_callbacks;

/**
 * Make request and maybe mirror the request based on the mirror policies of the route.
 * @param route supplies the route matched with the request.
 * @param command supplies the command of the request.
 * @param key supplies the key of the request.
 * @param incoming_request supplies the request.
 * @param callbacks supplies the request completion callbacks.
 * @return PoolRequest* a handle to the active request or nullptr if the request could not be made
 *         for some reason.
 */
Common::Redis::Client::PoolRequest* makeSingleServerRequest(
    const RouteSharedPtr& route, const std::string& command, const std::string& key,
    Common::Redis::RespValueConstSharedPtr incoming_request, ConnPool::PoolCallbacks& callbacks) {
  auto handler =
      route->upstream()->makeRequest(key, ConnPool::RespVariant(incoming_request), callbacks);
  if (handler) {
    for (auto& mirror_policy : route->mirrorPolicies()) {
      if (mirror_policy->shouldMirror(command)) {
        mirror_policy->upstream()->makeRequest(key, ConnPool::RespVariant(incoming_request),
                                               null_pool_callbacks);
      }
    }
  }
  return handler;
}

/**
 * Make request and maybe mirror the request based on the mirror policies of the route.
 * @param route supplies the route matched with the request.
 * @param command supplies the command of the request.
 * @param key supplies the key of the request.
 * @param incoming_request supplies the request.
 * @param callbacks supplies the request completion callbacks.
 * @return PoolRequest* a handle to the active request or nullptr if the request could not be made
 *         for some reason.
 */
Common::Redis::Client::PoolRequest*
makeFragmentedRequest(const RouteSharedPtr& route, const std::string& command,
                      const std::string& key, const Common::Redis::RespValue& incoming_request,
                      ConnPool::PoolCallbacks& callbacks) {
  auto handler =
      route->upstream()->makeRequest(key, ConnPool::RespVariant(incoming_request), callbacks);
  if (handler) {
    for (auto& mirror_policy : route->mirrorPolicies()) {
      if (mirror_policy->shouldMirror(command)) {
        mirror_policy->upstream()->makeRequest(key, ConnPool::RespVariant(incoming_request),
                                               null_pool_callbacks);
      }
    }
  }
  return handler;
}
} // namespace

void SplitRequestBase::onWrongNumberOfArguments(SplitCallbacks& callbacks,
                                                const Common::Redis::RespValue& request) {
  callbacks.onResponse(Common::Redis::Utility::makeError(
      fmt::format("wrong number of arguments for '{}' command", request.asArray()[0].asString())));
}

void SplitRequestBase::updateStats(const bool success) {
  if (success) {
    command_stats_.success_.inc();
  } else {
    command_stats_.error_.inc();
  }
  command_latency_->complete();
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
  callbacks_.onResponse(Common::Redis::Utility::makeError(Response::get().UpstreamFailure));
}

void SingleServerRequest::cancel() {
  handle_->cancel();
  handle_ = nullptr;
}

SplitRequestPtr SimpleRequest::create(Router& router,
                                      Common::Redis::RespValuePtr&& incoming_request,
                                      SplitCallbacks& callbacks, CommandStats& command_stats,
                                      TimeSource& time_source) {
  std::unique_ptr<SimpleRequest> request_ptr{
      new SimpleRequest(callbacks, command_stats, time_source)};

  const auto route = router.upstreamPool(incoming_request->asArray()[1].asString());
  if (route) {
    Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
    request_ptr->handle_ =
        makeSingleServerRequest(route, base_request->asArray()[0].asString(),
                                base_request->asArray()[1].asString(), base_request, *request_ptr);
  }

  if (!request_ptr->handle_) {
    callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
    return nullptr;
  }

  return request_ptr;
}

SplitRequestPtr EvalRequest::create(Router& router, Common::Redis::RespValuePtr&& incoming_request,
                                    SplitCallbacks& callbacks, CommandStats& command_stats,
                                    TimeSource& time_source) {
  // EVAL looks like: EVAL script numkeys key [key ...] arg [arg ...]
  // Ensure there are at least three args to the command or it cannot be hashed.
  if (incoming_request->asArray().size() < 4) {
    onWrongNumberOfArguments(callbacks, *incoming_request);
    command_stats.error_.inc();
    return nullptr;
  }

  std::unique_ptr<EvalRequest> request_ptr{new EvalRequest(callbacks, command_stats, time_source)};

  const auto route = router.upstreamPool(incoming_request->asArray()[3].asString());
  if (route) {
    Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
    request_ptr->handle_ =
        makeSingleServerRequest(route, base_request->asArray()[0].asString(),
                                base_request->asArray()[3].asString(), base_request, *request_ptr);
  }

  if (!request_ptr->handle_) {
    command_stats.error_.inc();
    callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
    return nullptr;
  }

  return request_ptr;
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
  onChildResponse(Common::Redis::Utility::makeError(Response::get().UpstreamFailure), index);
}

SplitRequestPtr MGETRequest::create(Router& router, Common::Redis::RespValuePtr&& incoming_request,
                                    SplitCallbacks& callbacks, CommandStats& command_stats,
                                    TimeSource& time_source) {
  std::unique_ptr<MGETRequest> request_ptr{new MGETRequest(callbacks, command_stats, time_source)};

  request_ptr->num_pending_responses_ = incoming_request->asArray().size() - 1;
  request_ptr->pending_requests_.reserve(request_ptr->num_pending_responses_);

  request_ptr->pending_response_ = std::make_unique<Common::Redis::RespValue>();
  request_ptr->pending_response_->type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> responses(request_ptr->num_pending_responses_);
  request_ptr->pending_response_->asArray().swap(responses);

  Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
  for (uint64_t i = 1; i < base_request->asArray().size(); i++) {
    request_ptr->pending_requests_.emplace_back(*request_ptr, i - 1);
    PendingRequest& pending_request = request_ptr->pending_requests_.back();

    const auto route = router.upstreamPool(base_request->asArray()[i].asString());
    if (route) {
      // Create composite array for a single get.
      const Common::Redis::RespValue single_mget(
          base_request, Common::Redis::Utility::GetRequest::instance(), i, i);
      pending_request.handle_ = makeFragmentedRequest(
          route, "get", base_request->asArray()[i].asString(), single_mget, pending_request);
    }

    if (!pending_request.handle_) {
      pending_request.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
    }
  }

  if (request_ptr->num_pending_responses_ > 0) {
    return request_ptr;
  }

  return nullptr;
}

void MGETRequest::onChildResponse(Common::Redis::RespValuePtr&& value, uint32_t index) {
  pending_requests_[index].handle_ = nullptr;

  pending_response_->asArray()[index].type(value->type());
  switch (value->type()) {
  case Common::Redis::RespType::Array:
  case Common::Redis::RespType::Integer:
  case Common::Redis::RespType::SimpleString:
  case Common::Redis::RespType::CompositeArray: {
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

SplitRequestPtr MSETRequest::create(Router& router, Common::Redis::RespValuePtr&& incoming_request,
                                    SplitCallbacks& callbacks, CommandStats& command_stats,
                                    TimeSource& time_source) {
  if ((incoming_request->asArray().size() - 1) % 2 != 0) {
    onWrongNumberOfArguments(callbacks, *incoming_request);
    command_stats.error_.inc();
    return nullptr;
  }
  std::unique_ptr<MSETRequest> request_ptr{new MSETRequest(callbacks, command_stats, time_source)};

  request_ptr->num_pending_responses_ = (incoming_request->asArray().size() - 1) / 2;
  request_ptr->pending_requests_.reserve(request_ptr->num_pending_responses_);

  request_ptr->pending_response_ = std::make_unique<Common::Redis::RespValue>();
  request_ptr->pending_response_->type(Common::Redis::RespType::SimpleString);

  Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
  unsigned fragment_index = 0;
  for (unsigned i = 1; i < base_request->asArray().size(); i += 2) {
    request_ptr->pending_requests_.emplace_back(*request_ptr, fragment_index++);
    PendingRequest& pending_request = request_ptr->pending_requests_.back();

    const auto route = router.upstreamPool(base_request->asArray()[i].asString());
    if (route) {
      // Create composite array for a single set command.
      const Common::Redis::RespValue single_set(
          base_request, Common::Redis::Utility::SetRequest::instance(), i, i + 1);
      ENVOY_LOG(debug, "redis: parallel set: '{}'", single_set.toString());
      pending_request.handle_ = makeFragmentedRequest(
          route, "set", base_request->asArray()[i].asString(), single_set, pending_request);
    }

    if (!pending_request.handle_) {
      pending_request.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
    }
  }

  if (request_ptr->num_pending_responses_ > 0) {
    return request_ptr;
  }

  return nullptr;
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
      callbacks_.onResponse(Common::Redis::Utility::makeError(
          fmt::format("finished with {} error(s)", error_count_)));
    }
  }
}

SplitRequestPtr SplitKeysSumResultRequest::create(Router& router,
                                                  Common::Redis::RespValuePtr&& incoming_request,
                                                  SplitCallbacks& callbacks,
                                                  CommandStats& command_stats,
                                                  TimeSource& time_source) {
  std::unique_ptr<SplitKeysSumResultRequest> request_ptr{
      new SplitKeysSumResultRequest(callbacks, command_stats, time_source)};

  request_ptr->num_pending_responses_ = incoming_request->asArray().size() - 1;
  request_ptr->pending_requests_.reserve(request_ptr->num_pending_responses_);

  request_ptr->pending_response_ = std::make_unique<Common::Redis::RespValue>();
  request_ptr->pending_response_->type(Common::Redis::RespType::Integer);

  Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
  for (unsigned i = 1; i < base_request->asArray().size(); i++) {
    request_ptr->pending_requests_.emplace_back(*request_ptr, i - 1);
    PendingRequest& pending_request = request_ptr->pending_requests_.back();

    // Create the composite array for a single fragment.
    const Common::Redis::RespValue single_fragment(base_request, base_request->asArray()[0], i, i);
    ENVOY_LOG(debug, "redis: parallel {}: '{}'", base_request->asArray()[0].asString(),
              single_fragment.toString());
    const auto route = router.upstreamPool(base_request->asArray()[i].asString());
    if (route) {
      pending_request.handle_ = makeFragmentedRequest(route, base_request->asArray()[0].asString(),
                                                      base_request->asArray()[i].asString(),
                                                      single_fragment, pending_request);
    }

    if (!pending_request.handle_) {
      pending_request.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
    }
  }

  if (request_ptr->num_pending_responses_ > 0) {
    return request_ptr;
  }

  return nullptr;
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
      callbacks_.onResponse(Common::Redis::Utility::makeError(
          fmt::format("finished with {} error(s)", error_count_)));
    }
  }
}

InstanceImpl::InstanceImpl(RouterPtr&& router, Stats::Scope& scope, const std::string& stat_prefix,
                           TimeSource& time_source, bool latency_in_micros)
    : router_(std::move(router)), simple_command_handler_(*router_),
      eval_command_handler_(*router_), mget_handler_(*router_), mset_handler_(*router_),
      split_keys_sum_result_handler_(*router_),
      stats_{ALL_COMMAND_SPLITTER_STATS(POOL_COUNTER_PREFIX(scope, stat_prefix + "splitter."))},
      time_source_(time_source) {
  for (const std::string& command : Common::Redis::SupportedCommands::simpleCommands()) {
    addHandler(scope, stat_prefix, command, latency_in_micros, simple_command_handler_);
  }

  for (const std::string& command : Common::Redis::SupportedCommands::evalCommands()) {
    addHandler(scope, stat_prefix, command, latency_in_micros, eval_command_handler_);
  }

  for (const std::string& command :
       Common::Redis::SupportedCommands::hashMultipleSumResultCommands()) {
    addHandler(scope, stat_prefix, command, latency_in_micros, split_keys_sum_result_handler_);
  }

  addHandler(scope, stat_prefix, Common::Redis::SupportedCommands::mget(), latency_in_micros,
             mget_handler_);

  addHandler(scope, stat_prefix, Common::Redis::SupportedCommands::mset(), latency_in_micros,
             mset_handler_);
}

SplitRequestPtr InstanceImpl::makeRequest(Common::Redis::RespValuePtr&& request,
                                          SplitCallbacks& callbacks) {
  if ((request->type() != Common::Redis::RespType::Array) || request->asArray().empty()) {
    onInvalidRequest(callbacks);
    return nullptr;
  }

  for (const Common::Redis::RespValue& value : request->asArray()) {
    if (value.type() != Common::Redis::RespType::BulkString) {
      onInvalidRequest(callbacks);
      return nullptr;
    }
  }

  std::string to_lower_string = absl::AsciiStrToLower(request->asArray()[0].asString());

  if (to_lower_string == Common::Redis::SupportedCommands::auth()) {
    if (request->asArray().size() < 2) {
      onInvalidRequest(callbacks);
      return nullptr;
    }
    callbacks.onAuth(request->asArray()[1].asString());
    return nullptr;
  }

  if (!callbacks.connectionAllowed()) {
    callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().AuthRequiredError));
    return nullptr;
  }

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

  auto handler = handler_lookup_table_.find(to_lower_string.c_str());
  if (handler == nullptr) {
    stats_.unsupported_command_.inc();
    callbacks.onResponse(Common::Redis::Utility::makeError(
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
  callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().InvalidRequest));
}

void InstanceImpl::addHandler(Stats::Scope& scope, const std::string& stat_prefix,
                              const std::string& name, bool latency_in_micros,
                              CommandHandler& handler) {
  std::string to_lower_name = absl::AsciiStrToLower(name);
  const std::string command_stat_prefix = fmt::format("{}command.{}.", stat_prefix, to_lower_name);
  Stats::StatNameManagedStorage storage{command_stat_prefix + std::string("latency"),
                                        scope.symbolTable()};
  handler_lookup_table_.add(
      to_lower_name.c_str(),
      std::make_shared<HandlerData>(HandlerData{
          CommandStats{ALL_COMMAND_STATS(POOL_COUNTER_PREFIX(scope, command_stat_prefix))
                           scope.histogramFromStatName(storage.statName(),
                                                       latency_in_micros
                                                           ? Stats::Histogram::Unit::Microseconds
                                                           : Stats::Histogram::Unit::Milliseconds)},
          handler}));
}

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
