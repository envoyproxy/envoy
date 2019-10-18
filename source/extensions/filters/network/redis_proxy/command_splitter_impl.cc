#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"

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

namespace {

// null_pool_callbacks is used for requests that must be filtered and not redirected such as
// "asking".
Common::Redis::Client::DoNothingPoolCallbacks null_pool_callbacks;

// Create an asking command request.
const Common::Redis::RespValue& askingRequest() {
  static Common::Redis::RespValue request;
  static bool initialized = false;

  if (!initialized) {
    Common::Redis::RespValue asking_cmd;
    asking_cmd.type(Common::Redis::RespType::BulkString);
    asking_cmd.asString() = "asking";
    request.type(Common::Redis::RespType::Array);
    request.asArray().push_back(asking_cmd);
    initialized = true;
  }
  return request;
}

/**
 * Validate the received moved/ask redirection error and the original redis request.
 * @param[in] original_request supplies the incoming request associated with the command splitter
 * request.
 * @param[in] error_response supplies the moved/ask redirection response from the upstream Redis
 * server.
 * @param[out] error_substrings the non-whitespace substrings of error_response.
 * @param[out] ask_redirection true if error_response is an ASK redirection error, false otherwise.
 * @return bool true if the original_request or error_response are not valid, false otherwise.
 */
bool redirectionArgsInvalid(const Common::Redis::RespValue* original_request,
                            const Common::Redis::RespValue& error_response,
                            std::vector<absl::string_view>& error_substrings,
                            bool& ask_redirection) {
  if ((original_request == nullptr) || (error_response.type() != Common::Redis::RespType::Error)) {
    return true;
  }
  error_substrings = StringUtil::splitToken(error_response.asString(), " ", false);
  if (error_substrings.size() != 3) {
    return true;
  }
  if (error_substrings[0] == "ASK") {
    ask_redirection = true;
  } else if (error_substrings[0] == "MOVED") {
    ask_redirection = false;
  } else {
    // The first substring must be MOVED or ASK.
    return true;
  }
  // Other validation done later to avoid duplicate processing.
  return false;
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
Common::Redis::Client::PoolRequest* makeRequest(const RouteSharedPtr& route,
                                                const std::string& command, const std::string& key,
                                                const Common::Redis::RespValue& incoming_request,
                                                Common::Redis::Client::PoolCallbacks& callbacks) {
  auto handler = route->upstream()->makeRequest(key, incoming_request, callbacks);
  if (handler) {
    for (auto& mirror_policy : route->mirrorPolicies()) {
      if (mirror_policy->shouldMirror(command)) {
        mirror_policy->upstream()->makeRequest(key, incoming_request, null_pool_callbacks);
      }
    }
  }
  return handler;
}
} // namespace

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
  callbacks_.onResponse(Utility::makeError(Response::get().UpstreamFailure));
}

bool SingleServerRequest::onRedirection(const Common::Redis::RespValue& value) {
  std::vector<absl::string_view> err;
  bool ask_redirection = false;
  if (redirectionArgsInvalid(incoming_request_.get(), value, err, ask_redirection) || !conn_pool_) {
    return false;
  }

  // MOVED and ASK redirection errors have the following substrings: MOVED or ASK (err[0]), hash key
  // slot (err[1]), and IP address and TCP port separated by a colon (err[2]).
  const std::string host_address = std::string(err[2]);

  // Prepend request with an asking command if redirected via an ASK error. The returned handle is
  // not important since there is no point in being able to cancel the request. The use of
  // null_pool_callbacks ensures the transparent filtering of the Redis server's response to the
  // "asking" command; this is fine since the server either responds with an OK or an error message
  // if cluster support is not enabled (in which case we should not get an ASK redirection error).
  if (ask_redirection &&
      !conn_pool_->makeRequestToHost(host_address, askingRequest(), null_pool_callbacks)) {
    return false;
  }
  handle_ = conn_pool_->makeRequestToHost(host_address, *incoming_request_, *this);

  if (handle_ != nullptr) {
    conn_pool_->onRedirection();
    return true;
  }
  return false;
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
    request_ptr->conn_pool_ = route->upstream();
    request_ptr->handle_ =
        makeRequest(route, incoming_request->asArray()[0].asString(),
                    incoming_request->asArray()[1].asString(), *incoming_request, *request_ptr);
  }

  if (!request_ptr->handle_) {
    callbacks.onResponse(Utility::makeError(Response::get().NoUpstreamHost));
    return nullptr;
  }

  request_ptr->incoming_request_ = std::move(incoming_request);
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
    request_ptr->conn_pool_ = route->upstream();
    request_ptr->handle_ =
        makeRequest(route, incoming_request->asArray()[0].asString(),
                    incoming_request->asArray()[3].asString(), *incoming_request, *request_ptr);
  }

  if (!request_ptr->handle_) {
    command_stats.error_.inc();
    callbacks.onResponse(Utility::makeError(Response::get().NoUpstreamHost));
    return nullptr;
  }

  request_ptr->incoming_request_ = std::move(incoming_request);
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
  onChildResponse(Utility::makeError(Response::get().UpstreamFailure), index);
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

  std::vector<Common::Redis::RespValue> values(2);
  values[0].type(Common::Redis::RespType::BulkString);
  values[0].asString() = "get";
  values[1].type(Common::Redis::RespType::BulkString);
  Common::Redis::RespValue single_mget;
  single_mget.type(Common::Redis::RespType::Array);
  single_mget.asArray().swap(values);

  for (uint64_t i = 1; i < incoming_request->asArray().size(); i++) {
    request_ptr->pending_requests_.emplace_back(*request_ptr, i - 1);
    PendingRequest& pending_request = request_ptr->pending_requests_.back();

    single_mget.asArray()[1].asString() = incoming_request->asArray()[i].asString();
    ENVOY_LOG(debug, "redis: parallel get: '{}'", single_mget.toString());
    const auto route = router.upstreamPool(incoming_request->asArray()[i].asString());
    if (route) {
      pending_request.conn_pool_ = route->upstream();
      pending_request.handle_ = makeRequest(route, "get", incoming_request->asArray()[i].asString(),
                                            single_mget, pending_request);
    }

    if (!pending_request.handle_) {
      pending_request.onResponse(Utility::makeError(Response::get().NoUpstreamHost));
    }
  }

  if (request_ptr->num_pending_responses_ > 0) {
    request_ptr->incoming_request_ = std::move(incoming_request);
    return request_ptr;
  }

  return nullptr;
}

bool FragmentedRequest::onChildRedirection(const Common::Redis::RespValue& value, uint32_t index,
                                           const ConnPool::InstanceSharedPtr& conn_pool) {
  std::vector<absl::string_view> err;
  bool ask_redirection = false;
  if (redirectionArgsInvalid(incoming_request_.get(), value, err, ask_redirection) || !conn_pool) {
    return false;
  }

  // MOVED and ASK redirection errors have the following substrings: MOVED or ASK (err[0]), hash key
  // slot (err[1]), and IP address and TCP port separated by a colon (err[2]).
  std::string host_address = std::string(err[2]);
  Common::Redis::RespValue request;
  recreate(request, index);

  // Prepend request with an asking command if redirected via an ASK error. The returned handle is
  // not important since there is no point in being able to cancel the request. The use of
  // null_pool_callbacks ensures the transparent filtering of the Redis server's response to the
  // "asking" command; this is fine since the server either responds with an OK or an error message
  // if cluster support is not enabled (in which case we should not get an ASK redirection error).
  if (ask_redirection &&
      !conn_pool->makeRequestToHost(host_address, askingRequest(), null_pool_callbacks)) {
    return false;
  }

  pending_requests_[index].handle_ =
      conn_pool->makeRequestToHost(host_address, request, pending_requests_[index]);

  if (pending_requests_[index].handle_ != nullptr) {
    conn_pool->onRedirection();
    return true;
  }
  return false;
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

void MGETRequest::recreate(Common::Redis::RespValue& request, uint32_t index) {
  static const uint32_t GET_COMMAND_SUBSTRINGS = 2;
  uint32_t num_values = GET_COMMAND_SUBSTRINGS;
  std::vector<Common::Redis::RespValue> values(num_values);

  for (uint32_t i = 0; i < num_values; i++) {
    values[i].type(Common::Redis::RespType::BulkString);
  }
  values[--num_values].asString() = incoming_request_->asArray()[index + 1].asString();
  values[--num_values].asString() = "get";

  request.type(Common::Redis::RespType::Array);
  request.asArray().swap(values);
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

  std::vector<Common::Redis::RespValue> values(3);
  values[0].type(Common::Redis::RespType::BulkString);
  values[0].asString() = "set";
  values[1].type(Common::Redis::RespType::BulkString);
  values[2].type(Common::Redis::RespType::BulkString);
  Common::Redis::RespValue single_mset;
  single_mset.type(Common::Redis::RespType::Array);
  single_mset.asArray().swap(values);

  uint64_t fragment_index = 0;
  for (uint64_t i = 1; i < incoming_request->asArray().size(); i += 2) {
    request_ptr->pending_requests_.emplace_back(*request_ptr, fragment_index++);
    PendingRequest& pending_request = request_ptr->pending_requests_.back();

    single_mset.asArray()[1].asString() = incoming_request->asArray()[i].asString();
    single_mset.asArray()[2].asString() = incoming_request->asArray()[i + 1].asString();

    ENVOY_LOG(debug, "redis: parallel set: '{}'", single_mset.toString());
    const auto route = router.upstreamPool(incoming_request->asArray()[i].asString());
    if (route) {
      pending_request.conn_pool_ = route->upstream();
      pending_request.handle_ = makeRequest(route, "set", incoming_request->asArray()[i].asString(),
                                            single_mset, pending_request);
    }

    if (!pending_request.handle_) {
      pending_request.onResponse(Utility::makeError(Response::get().NoUpstreamHost));
    }
  }

  if (request_ptr->num_pending_responses_ > 0) {
    request_ptr->incoming_request_ = std::move(incoming_request);
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
      callbacks_.onResponse(
          Utility::makeError(fmt::format("finished with {} error(s)", error_count_)));
    }
  }
}

void MSETRequest::recreate(Common::Redis::RespValue& request, uint32_t index) {
  static const uint32_t SET_COMMAND_SUBSTRINGS = 3;
  uint32_t num_values = SET_COMMAND_SUBSTRINGS;
  std::vector<Common::Redis::RespValue> values(num_values);

  for (uint32_t i = 0; i < num_values; i++) {
    values[i].type(Common::Redis::RespType::BulkString);
  }
  values[--num_values].asString() = incoming_request_->asArray()[(index * 2) + 2].asString();
  values[--num_values].asString() = incoming_request_->asArray()[(index * 2) + 1].asString();
  values[--num_values].asString() = "set";

  request.type(Common::Redis::RespType::Array);
  request.asArray().swap(values);
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

  std::vector<Common::Redis::RespValue> values(2);
  values[0].type(Common::Redis::RespType::BulkString);
  values[0].asString() = incoming_request->asArray()[0].asString();
  values[1].type(Common::Redis::RespType::BulkString);
  Common::Redis::RespValue single_fragment;
  single_fragment.type(Common::Redis::RespType::Array);
  single_fragment.asArray().swap(values);

  for (uint64_t i = 1; i < incoming_request->asArray().size(); i++) {
    request_ptr->pending_requests_.emplace_back(*request_ptr, i - 1);
    PendingRequest& pending_request = request_ptr->pending_requests_.back();

    single_fragment.asArray()[1].asString() = incoming_request->asArray()[i].asString();
    ENVOY_LOG(debug, "redis: parallel {}: '{}'", incoming_request->asArray()[0].asString(),
              single_fragment.toString());
    const auto route = router.upstreamPool(incoming_request->asArray()[i].asString());
    if (route) {
      pending_request.conn_pool_ = route->upstream();
      pending_request.handle_ =
          makeRequest(route, single_fragment.asArray()[0].asString(),
                      incoming_request->asArray()[i].asString(), single_fragment, pending_request);
    }

    if (!pending_request.handle_) {
      pending_request.onResponse(Utility::makeError(Response::get().NoUpstreamHost));
    }
  }

  if (request_ptr->num_pending_responses_ > 0) {
    request_ptr->incoming_request_ = std::move(incoming_request);
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
      callbacks_.onResponse(
          Utility::makeError(fmt::format("finished with {} error(s)", error_count_)));
    }
  }
}

void SplitKeysSumResultRequest::recreate(Common::Redis::RespValue& request, uint32_t index) {
  static const uint32_t BASE_COMMAND_SUBSTRINGS = 2;
  uint32_t num_values = BASE_COMMAND_SUBSTRINGS;
  std::vector<Common::Redis::RespValue> values(num_values);

  for (uint32_t i = 0; i < num_values; i++) {
    values[i].type(Common::Redis::RespType::BulkString);
  }
  values[--num_values].asString() = incoming_request_->asArray()[index + 1].asString();
  values[--num_values].asString() = incoming_request_->asArray()[0].asString();

  request.type(Common::Redis::RespType::Array);
  request.asArray().swap(values);
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

  std::string to_lower_string(request->asArray()[0].asString());
  to_lower_table_.toLowerCase(to_lower_string);

  if (to_lower_string == Common::Redis::SupportedCommands::auth()) {
    if (request->asArray().size() < 2) {
      onInvalidRequest(callbacks);
      return nullptr;
    }
    callbacks.onAuth(request->asArray()[1].asString());
    return nullptr;
  }

  if (!callbacks.connectionAllowed()) {
    callbacks.onResponse(Utility::makeError(Response::get().AuthRequiredError));
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
                              const std::string& name, bool latency_in_micros,
                              CommandHandler& handler) {
  std::string to_lower_name(name);
  to_lower_table_.toLowerCase(to_lower_name);
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
