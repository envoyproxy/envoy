#include "source/extensions/filters/network/redis_proxy/command_splitter_impl.h"

#include <chrono>
#include <cstdint>

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/common/redis/supported_commands.h"
#include "source/extensions/filters/network/redis_proxy/cluster_response_handler.h"

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
 * @param transaction supplies the transaction info of the current connection.
 * @return PoolRequest* a handle to the active request or nullptr if the request could not be made
 *         for some reason.
 */
Common::Redis::Client::PoolRequest* makeSingleServerRequest(
    const RouteSharedPtr& route, const std::string& command, const std::string& key,
    Common::Redis::RespValueConstSharedPtr incoming_request, ConnPool::PoolCallbacks& callbacks,
    Common::Redis::Client::Transaction& transaction) {
  // If a transaction is active, clients_[0] is the primary connection to the cluster.
  // The subsequent clients in the array are used for mirroring.
  transaction.current_client_idx_ = 0;
  auto handler = route->upstream(command)->makeRequest(key, ConnPool::RespVariant(incoming_request),
                                                       callbacks, transaction);
  if (handler) {
    for (auto& mirror_policy : route->mirrorPolicies()) {
      transaction.current_client_idx_++;
      if (mirror_policy->shouldMirror(command)) {
        mirror_policy->upstream()->makeRequest(key, ConnPool::RespVariant(incoming_request),
                                               null_pool_callbacks, transaction);
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
 * @param transaction supplies the transaction info of the current connection.
 * @return PoolRequest* a handle to the active request or nullptr if the request could not be made
 *         for some reason.
 */
Common::Redis::Client::PoolRequest*
makeFragmentedRequest(const RouteSharedPtr& route, const std::string& command,
                      const std::string& key, const Common::Redis::RespValue& incoming_request,
                      ConnPool::PoolCallbacks& callbacks,
                      Common::Redis::Client::Transaction& transaction) {
  auto handler = route->upstream(command)->makeRequest(key, ConnPool::RespVariant(incoming_request),
                                                       callbacks, transaction);
  if (handler) {
    for (auto& mirror_policy : route->mirrorPolicies()) {
      if (mirror_policy->shouldMirror(command)) {
        mirror_policy->upstream()->makeRequest(key, ConnPool::RespVariant(incoming_request),
                                               null_pool_callbacks, transaction);
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
 * @param transaction supplies the transaction info of the current connection.
 * @return PoolRequest* a handle to the active request or nullptr if the request could not be made
 *         for some reason.
 */
Common::Redis::Client::PoolRequest*
makeFragmentedRequestToShard(const RouteSharedPtr& route, const std::string& command,
                             uint16_t shard_index, const Common::Redis::RespValue& incoming_request,
                             ConnPool::PoolCallbacks& callbacks,
                             Common::Redis::Client::Transaction& transaction) {
  auto handler = route->upstream(command)->makeRequestToShard(
      shard_index, ConnPool::RespVariant(incoming_request), callbacks, transaction);
  if (handler) {
    for (auto& mirror_policy : route->mirrorPolicies()) {
      if (mirror_policy->shouldMirror(command)) {
        mirror_policy->upstream()->makeRequestToShard(
            shard_index, ConnPool::RespVariant(incoming_request), null_pool_callbacks, transaction);
      }
    }
  }
  return handler;
}

// Send a string response downstream.
void localResponse(SplitCallbacks& callbacks, std::string response) {
  Common::Redis::RespValuePtr res(new Common::Redis::RespValue());
  res->type(Common::Redis::RespType::SimpleString);
  res->asString() = response;
  callbacks.onResponse(std::move(res));
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
  if (command_latency_ != nullptr) {
    command_latency_->complete();
  }
}

SingleServerRequest::~SingleServerRequest() { ASSERT(!handle_); }

void SingleServerRequest::onResponse(Common::Redis::RespValuePtr&& response) {
  handle_ = nullptr;
  updateStats(true);
  callbacks_.onResponse(std::move(response));
}

void SingleServerRequest::onFailure() { onFailure(Response::get().UpstreamFailure); }

void SingleServerRequest::onFailure(std::string error_msg) {
  handle_ = nullptr;
  updateStats(false);
  callbacks_.onResponse(Common::Redis::Utility::makeError(error_msg));
}

void SingleServerRequest::cancel() {
  handle_->cancel();
  handle_ = nullptr;
}

SplitRequestPtr ErrorFaultRequest::create(SplitCallbacks& callbacks, CommandStats& command_stats,
                                          TimeSource& time_source, bool delay_command_latency,
                                          const StreamInfo::StreamInfo&) {
  std::unique_ptr<ErrorFaultRequest> request_ptr{
      new ErrorFaultRequest(callbacks, command_stats, time_source, delay_command_latency)};

  request_ptr->onFailure(Common::Redis::FaultMessages::get().Error);
  command_stats.error_fault_.inc();
  return nullptr;
}

std::unique_ptr<DelayFaultRequest>
DelayFaultRequest::create(SplitCallbacks& callbacks, CommandStats& command_stats,
                          TimeSource& time_source, Event::Dispatcher& dispatcher,
                          std::chrono::milliseconds delay, const StreamInfo::StreamInfo&) {
  return std::make_unique<DelayFaultRequest>(callbacks, command_stats, time_source, dispatcher,
                                             delay);
}

void DelayFaultRequest::onResponse(Common::Redis::RespValuePtr&& response) {
  response_ = std::move(response);
  delay_timer_->enableTimer(delay_);
}

void DelayFaultRequest::onDelayResponse() {
  command_stats_.delay_fault_.inc();
  command_latency_->complete(); // Complete latency of the command stats of the wrapped request
  callbacks_.onResponse(std::move(response_));
}

void DelayFaultRequest::cancel() { delay_timer_->disableTimer(); }

SplitRequestPtr SimpleRequest::create(Router& router,
                                      Common::Redis::RespValuePtr&& incoming_request,
                                      SplitCallbacks& callbacks, CommandStats& command_stats,
                                      TimeSource& time_source, bool delay_command_latency,
                                      const StreamInfo::StreamInfo& stream_info) {
  std::unique_ptr<SimpleRequest> request_ptr{
      new SimpleRequest(callbacks, command_stats, time_source, delay_command_latency)};
  const auto route = router.upstreamPool(incoming_request->asArray()[1].asString(), stream_info);
  if (route) {
    Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
    request_ptr->handle_ = makeSingleServerRequest(
        route, base_request->asArray()[0].asString(), base_request->asArray()[1].asString(),
        base_request, *request_ptr, callbacks.transaction());
  } else {
    ENVOY_LOG(debug, "route not found: '{}'", incoming_request->toString());
  }

  if (!request_ptr->handle_) {
    command_stats.error_.inc();
    callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
    return nullptr;
  }

  return request_ptr;
}

SplitRequestPtr EvalRequest::create(Router& router, Common::Redis::RespValuePtr&& incoming_request,
                                    SplitCallbacks& callbacks, CommandStats& command_stats,
                                    TimeSource& time_source, bool delay_command_latency,
                                    const StreamInfo::StreamInfo& stream_info) {
  // EVAL looks like: EVAL script numkeys key [key ...] arg [arg ...]
  // Ensure there are at least three args to the command or it cannot be hashed.
  if (incoming_request->asArray().size() < 4) {
    onWrongNumberOfArguments(callbacks, *incoming_request);
    command_stats.error_.inc();
    return nullptr;
  }

  std::unique_ptr<EvalRequest> request_ptr{
      new EvalRequest(callbacks, command_stats, time_source, delay_command_latency)};

  const auto route = router.upstreamPool(incoming_request->asArray()[3].asString(), stream_info);
  if (route) {
    Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
    request_ptr->handle_ = makeSingleServerRequest(
        route, base_request->asArray()[0].asString(), base_request->asArray()[3].asString(),
        base_request, *request_ptr, callbacks.transaction());
  }

  if (!request_ptr->handle_) {
    command_stats.error_.inc();
    callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
    return nullptr;
  }

  return request_ptr;
}

SplitRequestPtr ObjectRequest::create(Router& router,
                                      Common::Redis::RespValuePtr&& incoming_request,
                                      SplitCallbacks& callbacks, CommandStats& command_stats,
                                      TimeSource& time_source, bool delay_command_latency,
                                      const StreamInfo::StreamInfo& stream_info) {
  // OBJECT looks like: OBJECT subcommand key [arguments ...]
  // Ensure there are at least two args (subcommand and key) to the command.
  if (incoming_request->asArray().size() < 3) {
    onWrongNumberOfArguments(callbacks, *incoming_request);
    command_stats.error_.inc();
    return nullptr;
  }

  std::unique_ptr<ObjectRequest> request_ptr{
      new ObjectRequest(callbacks, command_stats, time_source, delay_command_latency)};

  const auto route = router.upstreamPool(incoming_request->asArray()[2].asString(), stream_info);
  if (route) {
    Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
    request_ptr->handle_ = makeSingleServerRequest(
        route, base_request->asArray()[0].asString(), base_request->asArray()[2].asString(),
        base_request, *request_ptr, callbacks.transaction());
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
                                    TimeSource& time_source, bool delay_command_latency,
                                    const StreamInfo::StreamInfo& stream_info) {
  std::unique_ptr<MGETRequest> request_ptr{
      new MGETRequest(callbacks, command_stats, time_source, delay_command_latency)};

  request_ptr->num_pending_responses_ = incoming_request->asArray().size() - 1;
  request_ptr->pending_requests_.reserve(request_ptr->num_pending_responses_);

  request_ptr->pending_response_ = std::make_unique<Common::Redis::RespValue>();
  request_ptr->pending_response_->type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> responses(request_ptr->num_pending_responses_);
  request_ptr->pending_response_->asArray().swap(responses);

  Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
  for (uint32_t i = 1; i < base_request->asArray().size(); i++) {
    request_ptr->pending_requests_.emplace_back(*request_ptr, i - 1);
    PendingRequest& pending_request = request_ptr->pending_requests_.back();

    const auto route = router.upstreamPool(base_request->asArray()[i].asString(), stream_info);
    if (route) {
      // Create composite array for a single get.
      const Common::Redis::RespValue single_mget(
          base_request, Common::Redis::Utility::GetRequest::instance(), i, i);
      pending_request.handle_ =
          makeFragmentedRequest(route, "get", base_request->asArray()[i].asString(), single_mget,
                                pending_request, callbacks.transaction());
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
    ENVOY_LOG(debug, "response: '{}'", pending_response_->toString());
    callbacks_.onResponse(std::move(pending_response_));
  }
}

SplitRequestPtr MSETRequest::create(Router& router, Common::Redis::RespValuePtr&& incoming_request,
                                    SplitCallbacks& callbacks, CommandStats& command_stats,
                                    TimeSource& time_source, bool delay_command_latency,
                                    const StreamInfo::StreamInfo& stream_info) {
  if ((incoming_request->asArray().size() - 1) % 2 != 0) {
    onWrongNumberOfArguments(callbacks, *incoming_request);
    command_stats.error_.inc();
    return nullptr;
  }
  std::unique_ptr<MSETRequest> request_ptr{
      new MSETRequest(callbacks, command_stats, time_source, delay_command_latency)};

  request_ptr->num_pending_responses_ = (incoming_request->asArray().size() - 1) / 2;
  request_ptr->pending_requests_.reserve(request_ptr->num_pending_responses_);

  request_ptr->pending_response_ = std::make_unique<Common::Redis::RespValue>();
  request_ptr->pending_response_->type(Common::Redis::RespType::SimpleString);

  Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
  uint32_t fragment_index = 0;
  for (uint32_t i = 1; i < base_request->asArray().size(); i += 2) {
    request_ptr->pending_requests_.emplace_back(*request_ptr, fragment_index++);
    PendingRequest& pending_request = request_ptr->pending_requests_.back();

    const auto route = router.upstreamPool(base_request->asArray()[i].asString(), stream_info);
    if (route) {
      // Create composite array for a single set command.
      const Common::Redis::RespValue single_set(
          base_request, Common::Redis::Utility::SetRequest::instance(), i, i + 1);
      ENVOY_LOG(debug, "parallel set: '{}'", single_set.toString());
      pending_request.handle_ =
          makeFragmentedRequest(route, "set", base_request->asArray()[i].asString(), single_set,
                                pending_request, callbacks.transaction());
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

SplitRequestPtr ScanRequest::create(Router& router, Common::Redis::RespValuePtr&& incoming_request,
                                    SplitCallbacks& callbacks, CommandStats& command_stats,
                                    TimeSource& time_source, bool delay_command_latency,
                                    const StreamInfo::StreamInfo& stream_info) {
  const auto route = router.upstreamPool(incoming_request->asArray()[1].asString(), stream_info);
  uint32_t shard_size =
      route ? route->upstream(incoming_request->asArray()[0].asString())->shardSize() : 0;
  if (shard_size == 0) {
    command_stats.error_.inc();
    callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
    return nullptr;
  }

  std::unique_ptr<ScanRequest> request_ptr{
      new ScanRequest(callbacks, command_stats, time_source, delay_command_latency)};
  request_ptr->num_pending_responses_ = shard_size;
  request_ptr->pending_requests_.reserve(request_ptr->num_pending_responses_);

  request_ptr->pending_response_ = std::make_unique<Common::Redis::RespValue>();
  request_ptr->pending_response_->type(Common::Redis::RespType::Array);

  Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
  for (uint32_t shard_index = 0; shard_index < shard_size; shard_index++) {
    request_ptr->pending_requests_.emplace_back(*request_ptr, shard_index);
    PendingRequest& pending_request = request_ptr->pending_requests_.back();

    ENVOY_LOG(debug, "scan request shard index {}: {}", shard_index, base_request->toString());
    pending_request.handle_ =
        makeFragmentedRequestToShard(route, base_request->asArray()[0].asString(), shard_index,
                                     *base_request, pending_request, callbacks.transaction());

    if (!pending_request.handle_) {
      pending_request.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
    }
  }

  if (request_ptr->num_pending_responses_ > 0) {
    return request_ptr;
  }

  return nullptr;
}

void ScanRequest::onChildResponse(Common::Redis::RespValuePtr&& value, uint32_t index) {
  pending_requests_[index].handle_ = nullptr;
  switch (value->type()) {
  case Common::Redis::RespType::Array: {
    pending_response_->asArray().insert(pending_response_->asArray().end(),
                                        value->asArray().begin(), value->asArray().end());
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
      callbacks_.onResponse(std::move(pending_response_));
    } else {
      callbacks_.onResponse(Common::Redis::Utility::makeError(
          fmt::format("finished with {} error(s)", error_count_)));
    }
  }
}

SplitRequestPtr ShardInfoRequest::create(Router& router,
                                         Common::Redis::RespValuePtr&& incoming_request,
                                         SplitCallbacks& callbacks, CommandStats& command_stats,
                                         TimeSource& time_source, bool delay_command_latency,
                                         const StreamInfo::StreamInfo& stream_info) {
  // Command format: INFO.SHARD <shard_id> [section]
  if (incoming_request->asArray().size() < 2 || incoming_request->asArray().size() > 3) {
    onWrongNumberOfArguments(callbacks, *incoming_request);
    command_stats.error_.inc();
    return nullptr;
  }

  // Parse shard_id (currently only supports numeric shard index)
  uint16_t shard_id = 0;
  if (!absl::SimpleAtoi(incoming_request->asArray()[1].asString(), &shard_id)) {
    callbacks.onResponse(
        Common::Redis::Utility::makeError("ERR invalid shard_id - must be a numeric shard index"));
    command_stats.error_.inc();
    return nullptr;
  }

  // Get route and verify shard_id is valid
  std::string empty_key = "";
  const auto route = router.upstreamPool(empty_key, stream_info);
  uint32_t shard_size = route ? route->upstream("info")->shardSize() : 0;
  if (shard_size == 0) {
    command_stats.error_.inc();
    callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
    return nullptr;
  }
  if (shard_id >= shard_size) {
    callbacks.onResponse(Common::Redis::Utility::makeError(
        fmt::format("ERR shard_id {} out of range (0-{})", shard_id, shard_size - 1)));
    command_stats.error_.inc();
    return nullptr;
  }

  std::unique_ptr<ShardInfoRequest> request_ptr{
      new ShardInfoRequest(callbacks, command_stats, time_source, delay_command_latency)};

  // We only send to one shard
  request_ptr->num_pending_responses_ = 1;
  request_ptr->pending_requests_.reserve(1);

  // Transform request: INFO.SHARD <shard_id> [section] -> INFO [section]
  Common::Redis::RespValuePtr info_request(new Common::Redis::RespValue());
  info_request->type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue>& info_array = info_request->asArray();

  // Add INFO command
  Common::Redis::RespValue info_cmd;
  info_cmd.type(Common::Redis::RespType::BulkString);
  info_cmd.asString() = "INFO";
  info_array.push_back(info_cmd);

  // Add optional section parameter if provided
  if (incoming_request->asArray().size() > 2) {
    info_array.push_back(incoming_request->asArray()[2]);
  }

  Common::Redis::RespValueSharedPtr base_request = std::move(info_request);
  request_ptr->pending_requests_.emplace_back(*request_ptr, shard_id);
  PendingRequest& pending_request = request_ptr->pending_requests_.back();

  ENVOY_LOG(debug, "shard info request to shard index {}: {}", shard_id, base_request->toString());
  pending_request.handle_ = makeFragmentedRequestToShard(route, "info", shard_id, *base_request,
                                                         pending_request, callbacks.transaction());

  if (!pending_request.handle_) {
    pending_request.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
  }

  if (request_ptr->num_pending_responses_ > 0) {
    return request_ptr;
  }

  return nullptr;
}

void ShardInfoRequest::onChildResponse(Common::Redis::RespValuePtr&& value, uint32_t index) {
  pending_requests_[0].handle_ = nullptr;

  // For shard info request, we simply forward the response directly from the single shard
  ASSERT(num_pending_responses_ > 0);
  ENVOY_LOG(debug, "shard info response from shard {}: '{}'", index, value->toString());

  updateStats(value->type() != Common::Redis::RespType::Error);
  callbacks_.onResponse(std::move(value));
}

SplitRequestPtr RandomShardRequest::create(Router& router,
                                           Common::Redis::RespValuePtr&& incoming_request,
                                           SplitCallbacks& callbacks, CommandStats& command_stats,
                                           TimeSource& time_source, bool delay_command_latency,
                                           const StreamInfo::StreamInfo& stream_info) {
  // Extract command first since we need it for routing
  const std::string command = absl::AsciiStrToLower(incoming_request->asArray()[0].asString());

  // First validate subcommands before any routing checks
  if (incoming_request->asArray().size() > 1) {
    const std::string subcommand = absl::AsciiStrToLower(incoming_request->asArray()[1].asString());
    // check is there is any subcommand restrictions for the command
    if (!Common::Redis::SupportedCommands::validateCommandSubcommands(command, subcommand)) {
      command_stats.error_.inc();
      callbacks.onResponse(Common::Redis::Utility::makeError(
          fmt::format("ERR {} subcommand '{}' is not supported", command, subcommand)));
      return nullptr;
    }
  }

  // Use default key (empty string) for routing since these commands aren't tied to specific keys
  std::string empty_key = "";
  const auto route = router.upstreamPool(empty_key, stream_info);
  uint32_t shard_size = route ? route->upstream(command)->shardSize() : 0;
  if (shard_size == 0) {
    command_stats.error_.inc();
    callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
    return nullptr;
  }

  std::unique_ptr<RandomShardRequest> request_ptr{
      new RandomShardRequest(callbacks, command_stats, time_source, delay_command_latency)};

  // Set up for single shard request - only one pending response
  request_ptr->num_pending_responses_ = 1;
  request_ptr->pending_requests_.reserve(1);

  // Select a random shard index using time-based pseudo-randomness
  // This provides good distribution without needing access to RandomGenerator
  auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                 time_source.systemTime().time_since_epoch())
                 .count();
  uint32_t random_shard_index = static_cast<uint32_t>(now) % shard_size;

  // Create single pending request with the random shard index
  request_ptr->pending_requests_.emplace_back(*request_ptr, random_shard_index);
  PendingRequest& pending_request = request_ptr->pending_requests_.back();

  Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
  ENVOY_LOG(debug, "random shard request to shard index {}: {}", random_shard_index,
            base_request->toString());

  // Send request to the randomly selected shard
  pending_request.handle_ = makeFragmentedRequestToShard(
      route, command, random_shard_index, *base_request, pending_request, callbacks.transaction());

  if (!pending_request.handle_) {
    pending_request.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
  }

  if (request_ptr->num_pending_responses_ > 0) {
    return request_ptr;
  }

  return nullptr;
}

void RandomShardRequest::onChildResponse(Common::Redis::RespValuePtr&& value, uint32_t index) {

  pending_requests_[0].handle_ = nullptr; // We only have one request at pending_requests_[0]

  // For random shard requests, we simply forward the response directly
  // No aggregation or special processing needed
  ASSERT(num_pending_responses_ > 0);
  // index is the shard_index that responded (can be any value 0 to shard_size-1)
  ENVOY_LOG(debug, "random shard response from shard {}: '{}'", index, value->toString());

  updateStats(value->type() != Common::Redis::RespType::Error);
  callbacks_.onResponse(std::move(value));
}

SplitRequestPtr ClusterScopeCmdRequest::create(Router& router,
                                               Common::Redis::RespValuePtr&& incoming_request,
                                               SplitCallbacks& callbacks,
                                               CommandStats& command_stats, TimeSource& time_source,
                                               bool delay_command_latency,
                                               const StreamInfo::StreamInfo& stream_info) {

  const std::string command = absl::AsciiStrToLower(incoming_request->asArray()[0].asString());
  if (incoming_request->asArray().size() > 1) {
    const std::string subcommand = absl::AsciiStrToLower(incoming_request->asArray()[1].asString());
    // check is there is any subcommand restrictions for the command
    if (!Common::Redis::SupportedCommands::validateCommandSubcommands(command, subcommand)) {
      command_stats.error_.inc();
      callbacks.onResponse(Common::Redis::Utility::makeError(
          fmt::format("ERR {} subcommand '{}' is not supported", command, subcommand)));
      return nullptr;
    }
  }

  // Use a default key (empty string) for routing  cluster scope commands
  // are not tied to specific keys. This relies on having a catch_all_route configured and no prefix
  // set as "".
  uint32_t shard_size = 0;

  std::string empty_key = "";
  const auto route = router.upstreamPool(empty_key, stream_info);

  shard_size = route ? route->upstream(command)->shardSize() : 0;
  if (shard_size == 0) {
    command_stats.error_.inc();
    callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
    return nullptr;
  }

  std::unique_ptr<ClusterScopeCmdRequest> request_ptr{
      new ClusterScopeCmdRequest(callbacks, command_stats, time_source, delay_command_latency)};

  // Initialize the response handler based on the incoming request and shard size
  if (!request_ptr->initializeResponseHandler(*incoming_request, shard_size)) {
    command_stats.error_.inc();
    callbacks.onResponse(Common::Redis::Utility::makeError(
        "ERR unsupported cluster scope command or invalid arguments"));
    return nullptr;
  }

  request_ptr->pending_requests_.reserve(shard_size);

  Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
  for (uint32_t shard_index = 0; shard_index < shard_size; shard_index++) {
    request_ptr->pending_requests_.emplace_back(*request_ptr, shard_index);
    PendingRequest& pending_request = request_ptr->pending_requests_.back();

    // Send the same request to all shards one by one
    pending_request.handle_ = makeFragmentedRequestToShard(
        route, command, shard_index, *base_request, pending_request, callbacks.transaction());

    if (!pending_request.handle_) {
      ENVOY_LOG(error, "{}:failed to create request handle for shard index {}: '{}'", __func__,
                shard_index, base_request->toString());
      pending_request.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
    }
  }

  return request_ptr;
}

SplitRequestPtr
SplitKeysSumResultRequest::create(Router& router, Common::Redis::RespValuePtr&& incoming_request,
                                  SplitCallbacks& callbacks, CommandStats& command_stats,
                                  TimeSource& time_source, bool delay_command_latency,
                                  const StreamInfo::StreamInfo& stream_info) {
  std::unique_ptr<SplitKeysSumResultRequest> request_ptr{
      new SplitKeysSumResultRequest(callbacks, command_stats, time_source, delay_command_latency)};

  request_ptr->num_pending_responses_ = incoming_request->asArray().size() - 1;
  request_ptr->pending_requests_.reserve(request_ptr->num_pending_responses_);

  request_ptr->pending_response_ = std::make_unique<Common::Redis::RespValue>();
  request_ptr->pending_response_->type(Common::Redis::RespType::Integer);

  Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
  for (uint32_t i = 1; i < base_request->asArray().size(); i++) {
    request_ptr->pending_requests_.emplace_back(*request_ptr, i - 1);
    PendingRequest& pending_request = request_ptr->pending_requests_.back();

    // Create the composite array for a single fragment.
    const Common::Redis::RespValue single_fragment(base_request, base_request->asArray()[0], i, i);
    ENVOY_LOG(debug, "parallel {}: '{}'", base_request->asArray()[0].asString(),
              single_fragment.toString());
    const auto route = router.upstreamPool(base_request->asArray()[i].asString(), stream_info);
    if (route) {
      pending_request.handle_ = makeFragmentedRequest(
          route, base_request->asArray()[0].asString(), base_request->asArray()[i].asString(),
          single_fragment, pending_request, callbacks.transaction());
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

SplitRequestPtr TransactionRequest::create(Router& router,
                                           Common::Redis::RespValuePtr&& incoming_request,
                                           SplitCallbacks& callbacks, CommandStats& command_stats,
                                           TimeSource& time_source, bool delay_command_latency,
                                           const StreamInfo::StreamInfo& stream_info) {
  Common::Redis::Client::Transaction& transaction = callbacks.transaction();
  std::string command_name = absl::AsciiStrToLower(incoming_request->asArray()[0].asString());

  // Within transactions we only support simple commands.
  // So if this is not a transaction command or a simple command, it is an error.
  // We also support multi-key commands, but will leave it to the client to handle the case where
  // the keys provided are not from the same shard.
  if (Common::Redis::SupportedCommands::transactionCommands().count(command_name) == 0 &&
      Common::Redis::SupportedCommands::simpleCommands().count(command_name) == 0 &&
      Common::Redis::SupportedCommands::multiKeyCommands().count(command_name) == 0) {
    callbacks.onResponse(Common::Redis::Utility::makeError(
        fmt::format("'{}' command is not supported within transaction",
                    incoming_request->asArray()[0].asString())));
    return nullptr;
  }

  // Start transaction on MULTI, and stop on EXEC/DISCARD.
  if (command_name == "multi") {
    // Check for nested MULTI commands.
    if (transaction.active_) {
      callbacks.onResponse(
          Common::Redis::Utility::makeError(fmt::format("MULTI calls can not be nested")));
      return nullptr;
    }
    transaction.start();

    // If we already have a key set (from a previous WATCH command), we will send the actual MULTI
    // upstream. Otherwise, we will respond locally with "OK".
    if (transaction.key_.empty()) {
      localResponse(callbacks, "OK");
      return nullptr;
    } else {
      RouteSharedPtr route = router.upstreamPool(transaction.key_, stream_info);
      if (route) {
        // We reserve a client for the main connection and for each mirror connection.
        transaction.clients_.resize(1 + route->mirrorPolicies().size());
      }
    }
  } else if (command_name == "exec" || command_name == "discard") {
    // Handle the case where we don't have an open transaction.
    if (transaction.active_ == false) {
      callbacks.onResponse(Common::Redis::Utility::makeError(
          fmt::format("{} without MULTI", absl::AsciiStrToUpper(command_name))));
      return nullptr;
    }

    // Handle the case where the transaction is empty.
    if (transaction.key_.empty()) {
      if (command_name == "exec") {
        Common::Redis::RespValuePtr empty_array{new Common::Redis::Client::EmptyArray{}};
        callbacks.onResponse(std::move(empty_array));
      } else {
        localResponse(callbacks, "OK");
      }
      transaction.close();
      return nullptr;
    }

    // In all other cases we will close the transaction connection after sending the last command.
    transaction.should_close_ = true;
  }

  // If we do a WATCH command without having started a transaction, we send it upstream and save the
  // key, so we can support UNWATCH. We have to also set the connection details.
  if (command_name == "watch" && !transaction.active_) {
    transaction.key_ = incoming_request->asArray()[1].asString();
  }

  // When we receive the first command with a key we will set this key as our transaction
  // key, and then send a MULTI command to the node that handles that key.
  // The response for the MULTI command will be discarded since we pass the null_pool_callbacks
  // to the handler.
  RouteSharedPtr route;
  if (transaction.key_.empty()) {
    // If we do an UNWATCH and we don't have a key until now, this is a no-op.
    if (command_name == "unwatch") {
      if (transaction.active_) {
        // No-op during transaction -> QUEUED
        localResponse(callbacks, "QUEUED");
      } else {
        // No-op outside transaction -> OK
        localResponse(callbacks, "OK");
      }

      return nullptr;
    }

    transaction.key_ = incoming_request->asArray()[1].asString();
    route = router.upstreamPool(transaction.key_, stream_info);
    Common::Redis::RespValueSharedPtr multi_request =
        std::make_shared<Common::Redis::Client::MultiRequest>();
    if (route) {
      // We reserve a client for the main connection and for each mirror connection.
      transaction.clients_.resize(1 + route->mirrorPolicies().size());
      makeSingleServerRequest(route, "MULTI", transaction.key_, multi_request, null_pool_callbacks,
                              callbacks.transaction());
      transaction.connection_established_ = true;
    }
  } else {
    route = router.upstreamPool(transaction.key_, stream_info);
  }

  std::unique_ptr<TransactionRequest> request_ptr{
      new TransactionRequest(callbacks, command_stats, time_source, delay_command_latency)};

  if (route) {
    Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
    request_ptr->handle_ =
        makeSingleServerRequest(route, base_request->asArray()[0].asString(), transaction.key_,
                                base_request, *request_ptr, callbacks.transaction());
  }

  // If we send an UNWATCH outside of a transaction, we clear the transaction key.
  if (command_name == "unwatch" && !transaction.active_) {
    transaction.key_.clear();
  }

  if (!request_ptr->handle_) {
    command_stats.error_.inc();
    callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().NoUpstreamHost));
    return nullptr;
  }

  // If we sent a MULTI command upstream, connection was established.
  if (command_name == "multi") {
    transaction.connection_established_ = true;
  }

  return request_ptr;
}

InstanceImpl::InstanceImpl(RouterPtr&& router, Stats::Scope& scope, const std::string& stat_prefix,
                           TimeSource& time_source, bool latency_in_micros,
                           Common::Redis::FaultManagerPtr&& fault_manager,
                           absl::flat_hash_set<std::string>&& custom_commands)
    : router_(std::move(router)), simple_command_handler_(*router_),
      eval_command_handler_(*router_), object_command_handler_(*router_), mget_handler_(*router_),
      mset_handler_(*router_), scan_handler_(*router_), shard_info_handler_(*router_),
      random_shard_handler_(*router_), split_keys_sum_result_handler_(*router_),
      transaction_handler_(*router_), cluster_scope_handler_(*router_),
      stats_{ALL_COMMAND_SPLITTER_STATS(POOL_COUNTER_PREFIX(scope, stat_prefix + "splitter."))},
      time_source_(time_source), fault_manager_(std::move(fault_manager)),
      custom_commands_(std::move(custom_commands)) {
  for (const std::string& command : Common::Redis::SupportedCommands::simpleCommands()) {
    addHandler(scope, stat_prefix, command, latency_in_micros, simple_command_handler_);
  }

  for (const std::string& command : Common::Redis::SupportedCommands::evalCommands()) {
    addHandler(scope, stat_prefix, command, latency_in_micros, eval_command_handler_);
  }

  for (const std::string& command : Common::Redis::SupportedCommands::objectCommands()) {
    addHandler(scope, stat_prefix, command, latency_in_micros, object_command_handler_);
  }

  for (const std::string& command :
       Common::Redis::SupportedCommands::hashMultipleSumResultCommands()) {
    addHandler(scope, stat_prefix, command, latency_in_micros, split_keys_sum_result_handler_);
  }

  addHandler(scope, stat_prefix, Common::Redis::SupportedCommands::mget(), latency_in_micros,
             mget_handler_);

  addHandler(scope, stat_prefix, Common::Redis::SupportedCommands::mset(), latency_in_micros,
             mset_handler_);

  addHandler(scope, stat_prefix, Common::Redis::SupportedCommands::scan(), latency_in_micros,
             scan_handler_);

  addHandler(scope, stat_prefix, Common::Redis::SupportedCommands::infoShard(), latency_in_micros,
             shard_info_handler_);

  for (const std::string& command : Common::Redis::SupportedCommands::randomShardCommands()) {
    addHandler(scope, stat_prefix, command, latency_in_micros, random_shard_handler_);
  }

  for (const std::string& command : Common::Redis::SupportedCommands::transactionCommands()) {
    addHandler(scope, stat_prefix, command, latency_in_micros, transaction_handler_);
  }

  for (const std::string& command : Common::Redis::SupportedCommands::ClusterScopeCommands()) {
    addHandler(scope, stat_prefix, command, latency_in_micros, cluster_scope_handler_);
  }

  for (const std::string& command : custom_commands_) {
    // treating custom commands to be simple commands for now
    addHandler(scope, stat_prefix, command, latency_in_micros, simple_command_handler_);
  }
}

SplitRequestPtr InstanceImpl::makeRequest(Common::Redis::RespValuePtr&& request,
                                          SplitCallbacks& callbacks, Event::Dispatcher& dispatcher,
                                          const StreamInfo::StreamInfo& stream_info) {
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

  std::string command_name = absl::AsciiStrToLower(request->asArray()[0].asString());
  // Compatible with redis behavior, if there is an unsupported command, return immediately,
  // this action must be performed before verifying auth, some redis clients rely on this behavior.
  if (!Common::Redis::SupportedCommands::isSupportedCommand(command_name) &&
      custom_commands_.find(command_name) == custom_commands_.end()) {
    stats_.unsupported_command_.inc();
    callbacks.onResponse(Common::Redis::Utility::makeError(fmt::format(
        "ERR unknown command '{}', with args beginning with: {}", request->asArray()[0].asString(),
        request->asArray().size() > 1 ? request->asArray()[1].asString() : "")));
    return nullptr;
  }

  if (command_name == Common::Redis::SupportedCommands::auth()) {
    if (request->asArray().size() < 2) {
      onInvalidRequest(callbacks);
      return nullptr;
    }
    if (request->asArray().size() == 3) {
      callbacks.onAuth(request->asArray()[1].asString(), request->asArray()[2].asString());
    } else {
      callbacks.onAuth(request->asArray()[1].asString());
    }

    return nullptr;
  }

  if (!callbacks.connectionAllowed()) {
    callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().AuthRequiredError));
    return nullptr;
  }

  if (command_name == Common::Redis::SupportedCommands::ping()) {
    // Respond to PING locally.
    Common::Redis::RespValuePtr pong(new Common::Redis::RespValue());
    pong->type(Common::Redis::RespType::SimpleString);
    pong->asString() = "PONG";
    callbacks.onResponse(std::move(pong));
    return nullptr;
  }

  if (command_name == Common::Redis::SupportedCommands::echo()) {
    // Respond to ECHO locally.
    if (request->asArray().size() != 2) {
      onInvalidRequest(callbacks);
      return nullptr;
    }
    Common::Redis::RespValuePtr echo_resp(new Common::Redis::RespValue());
    echo_resp->type(Common::Redis::RespType::BulkString);
    echo_resp->asString() = request->asArray()[1].asString();
    callbacks.onResponse(std::move(echo_resp));
    return nullptr;
  }

  if (command_name == Common::Redis::SupportedCommands::time()) {
    // Respond to TIME locally.
    Common::Redis::RespValuePtr time_resp(new Common::Redis::RespValue());
    time_resp->type(Common::Redis::RespType::Array);
    std::vector<Common::Redis::RespValue> resp_array;

    auto now = dispatcher.timeSource().systemTime().time_since_epoch();

    Common::Redis::RespValue time_in_secs;
    time_in_secs.type(Common::Redis::RespType::BulkString);
    time_in_secs.asString() =
        std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now).count());
    resp_array.push_back(time_in_secs);

    Common::Redis::RespValue time_in_micro_secs;
    time_in_micro_secs.type(Common::Redis::RespType::BulkString);
    time_in_micro_secs.asString() =
        std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
    resp_array.push_back(time_in_micro_secs);

    time_resp->asArray().swap(resp_array);
    callbacks.onResponse(std::move(time_resp));
    return nullptr;
  }

  if (command_name == Common::Redis::SupportedCommands::scan()) {
    if (request->asArray().size() < 2) {
      callbacks.onResponse(Common::Redis::Utility::makeError(fmt::format(
          "ERR wrong number of arguments for '{}' command", request->asArray()[0].asString())));
      return nullptr;
    }
  }

  if (command_name == Common::Redis::SupportedCommands::quit()) {
    callbacks.onQuit();
    return nullptr;
  }

  if (request->asArray().size() < 2 &&
      !Common::Redis::SupportedCommands::isCommandValidWithoutArgs(command_name)) {
    // Commands that require at least one argument beyond the command name
    onInvalidRequest(callbacks);
    return nullptr;
  }

  // Get the handler for the downstream request
  auto handler = handler_lookup_table_.find(command_name.c_str());
  ASSERT(handler != nullptr);

  // If we are within a transaction, forward all requests to the transaction handler (i.e. handler
  // of "multi" command).
  if (callbacks.transaction().active_) {
    handler = handler_lookup_table_.find("multi");
  }

  // Fault Injection Check
  const Common::Redis::Fault* fault_ptr = fault_manager_->getFaultForCommand(command_name);

  // Check if delay, which determines which callbacks to use. If a delay fault is enabled,
  // the delay fault itself wraps the request (or other fault) and the delay fault itself
  // implements the callbacks functions, and in turn calls the real callbacks after injecting
  // delay on the result of the wrapped request or fault.
  const bool has_delay_fault =
      fault_ptr != nullptr && fault_ptr->delayMs() > std::chrono::milliseconds(0);
  std::unique_ptr<DelayFaultRequest> delay_fault_ptr;
  if (has_delay_fault) {
    delay_fault_ptr = DelayFaultRequest::create(callbacks, handler->command_stats_, time_source_,
                                                dispatcher, fault_ptr->delayMs(), stream_info);
  }

  // Note that the command_stats_ object of the original request is used for faults, so that our
  // downstream metrics reflect any faults added (with special fault metrics) or extra latency from
  // a delay. 2) we use a ternary operator for the callback parameter- we want to use the
  // delay_fault as callback if there is a delay per the earlier comment.
  ENVOY_LOG(debug, "splitting '{}'", request->toString());
  handler->command_stats_.total_.inc();

  SplitRequestPtr request_ptr;
  if (fault_ptr != nullptr && fault_ptr->faultType() == Common::Redis::FaultType::Error) {
    request_ptr = ErrorFaultRequest::create(has_delay_fault ? *delay_fault_ptr : callbacks,
                                            handler->command_stats_, time_source_, has_delay_fault,
                                            stream_info);
  } else {
    request_ptr = handler->handler_.get().startRequest(
        std::move(request), has_delay_fault ? *delay_fault_ptr : callbacks, handler->command_stats_,
        time_source_, has_delay_fault, stream_info);
  }

  // Complete delay, if any. The delay fault takes ownership of the wrapped request.
  if (has_delay_fault) {
    delay_fault_ptr->wrapped_request_ptr_ = std::move(request_ptr);
    return delay_fault_ptr;
  } else {
    return request_ptr;
  }
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
