#include "source/extensions/filters/network/redis_proxy/command_splitter_impl.h"

#include <chrono>
#include <cstdint>

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/common/redis/supported_commands.h"
#include "source/extensions/filters/network/redis_proxy/cluster_response_handler.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace CommandSplitter {
namespace {

// null_pool_callbacks is used for requests that must be filtered and not redirected such as
// "asking".
ConnPool::DoNothingPoolCallbacks null_pool_callbacks;

// HELLO reply field names. Keeping them as constexpr string_views centralizes
// the wire-shape contract and avoids string-literal duplication across the
// reply builder and its tests.
constexpr absl::string_view kHelloFieldServer = "server";
constexpr absl::string_view kHelloFieldVersion = "version";
constexpr absl::string_view kHelloFieldProto = "proto";
constexpr absl::string_view kHelloFieldId = "id";
constexpr absl::string_view kHelloFieldMode = "mode";
constexpr absl::string_view kHelloFieldRole = "role";
constexpr absl::string_view kHelloFieldModules = "modules";

// The proxy advertises itself with these stable values. The Redis-compatible version string is
// what clients gate feature detection on, and the proxy fronts backends whose versions it cannot
// know — so advertise the floor implied by RESP3 support (introduced in 6.0.0), not a ceiling: a
// higher number would entice clients to issue 7.x-only commands that a 6.x backend rejects at
// runtime. The real Envoy build version is available via /server_info.
constexpr absl::string_view kHelloServerName = "envoy-redis-proxy";
constexpr absl::string_view kHelloRedisCompatVersion = "6.0.0";
// Per the RESP3 spec, ``HELLO mode`` must be one of ``standalone`` / ``sentinel`` / ``cluster``
// (https://redis.io/docs/latest/develop/reference/protocol-spec/). Strict clients reject other
// values. Envoy presents one logical Redis endpoint and hides Redis Cluster routing internally,
// so ``standalone`` is the truthful value to advertise — clients should not attempt cluster-mode
// auto-discovery against the proxy.
constexpr absl::string_view kHelloMode = "standalone";
constexpr absl::string_view kHelloRole = "master";

// Option keywords that may follow ``HELLO [protover]``; matched
// case-insensitively against the client-supplied tokens.
constexpr absl::string_view kHelloOptionAuth = "AUTH";
constexpr absl::string_view kHelloOptionSetname = "SETNAME";

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

// Build the HELLO reply locally as a Map: a RESP3 downstream gets `%7\r\n...`; the encoder
// down-converts to a `*14\r\n...` flat array for RESP2. `id` is hardcoded to 0 — a multiplexed
// proxy has no single Redis CLIENT ID to expose. At namespace scope (not anonymous) because
// ProxyFilter also calls it to emit the deferred HELLO reply after external `HELLO N AUTH`.
Common::Redis::RespValuePtr buildHelloReply(uint32_t downstream_version) {
  auto reply = std::make_unique<Common::Redis::RespValue>();
  reply->type(Common::Redis::RespType::Map);

  std::vector<Common::Redis::RespValue> kv;
  kv.reserve(14);
  kv.push_back(Common::Redis::Utility::makeBulkString(kHelloFieldServer));
  kv.push_back(Common::Redis::Utility::makeBulkString(kHelloServerName));
  kv.push_back(Common::Redis::Utility::makeBulkString(kHelloFieldVersion));
  kv.push_back(Common::Redis::Utility::makeBulkString(kHelloRedisCompatVersion));
  kv.push_back(Common::Redis::Utility::makeBulkString(kHelloFieldProto));
  kv.push_back(Common::Redis::Utility::makeInteger(downstream_version));
  kv.push_back(Common::Redis::Utility::makeBulkString(kHelloFieldId));
  kv.push_back(Common::Redis::Utility::makeInteger(0));
  kv.push_back(Common::Redis::Utility::makeBulkString(kHelloFieldMode));
  kv.push_back(Common::Redis::Utility::makeBulkString(kHelloMode));
  kv.push_back(Common::Redis::Utility::makeBulkString(kHelloFieldRole));
  kv.push_back(Common::Redis::Utility::makeBulkString(kHelloRole));
  kv.push_back(Common::Redis::Utility::makeBulkString(kHelloFieldModules));
  Common::Redis::RespValue modules;
  modules.type(Common::Redis::RespType::Array);
  kv.push_back(std::move(modules));
  reply->asArray().swap(kv);
  return reply;
}

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

void DelayFaultRequest::respond(RespValueFrames&& frames) {
  frames_ = std::move(frames);
  delay_timer_->enableTimer(delay_);
}

void DelayFaultRequest::onDelayResponse() {
  command_stats_.delay_fault_.inc();
  command_latency_->complete(); // Complete latency of the command stats of the wrapped request
  callbacks_.respond(std::move(frames_));
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

SplitRequestPtr PublishRequest::create(Router& router,
                                       Common::Redis::RespValuePtr&& incoming_request,
                                       SplitCallbacks& callbacks, CommandStats& command_stats,
                                       TimeSource& time_source, bool delay_command_latency,
                                       const StreamInfo::StreamInfo& stream_info) {
  // PUBLISH/SPUBLISH: ``<verb> channel value``. Own the arity check here so the wording
  // matches every other handler regardless of whether the splitter's generic ``size() < 2``
  // guard ran first (publish/spublish are exempted from that guard, see InstanceImpl::
  // makeRequest, so this is the only arity check that fires).
  if (incoming_request->asArray().size() != 3) {
    onWrongNumberOfArguments(callbacks, *incoming_request);
    command_stats.error_.inc();
    return nullptr;
  }

  std::unique_ptr<PublishRequest> request_ptr{
      new PublishRequest(callbacks, command_stats, time_source, delay_command_latency)};

  // ``router.upstreamPool(std::string& key, ...)`` may rewrite ``key`` in place via
  // ``key_formatter`` / ``remove_prefix``. ``channel`` is a reference to the request's channel
  // element, so whether that rewrite reaches the upstream wire depends on WHICH string we route on
  // — and the two publish modes want opposite things (F4):
  const std::string& channel = incoming_request->asArray()[1].asString();
  // The sharded WIRE path is taken when the client sent ``SPUBLISH`` explicitly OR when
  // ``enable_sharded_publish`` rewrites ``PUBLISH`` to ``SPUBLISH``. An explicit ``SPUBLISH`` is a
  // shard command by definition, so it must use the sharded path REGARDLESS of the config flag —
  // otherwise (flag off) it would fall to the classic branch below and a ``remove_prefix`` /
  // ``key_formatter`` route would rewrite its channel, silently sending it to the wrong shard and
  // breaking the shard-channel contract.
  // Case-insensitive compare instead of AsciiStrToLower + == so no lowercased copy is allocated per
  // PUBLISH (E-4); makeRequest already lowercased the dispatch verb, but that value is not threaded
  // into the handler factory.
  const bool is_spublish = absl::EqualsIgnoreCase(incoming_request->asArray()[0].asString(),
                                                  Common::Redis::SupportedCommands::spublish());
  const bool use_sharded_wire = is_spublish || callbacks.shardedPublishEnabled();
  RouteSharedPtr route;
  if (use_sharded_wire) {
    // Sharded pub/sub delivers to ``SSUBSCRIBE`` subscribers on the slot-owning shard. The channel
    // drives slot hashing and the SPUBLISH wire request and MUST stay the unmodified original:
    // SSUBSCRIBE returns the client's channel verbatim and hashes it un-stripped, so a
    // ``remove_prefix`` / ``key_formatter`` here would desync PUBLISH from SUBSCRIBE and break
    // delivery. Route on a COPY so any rewrite is discarded and ``channel`` stays original.
    std::string route_key = channel;
    route = router.upstreamPool(route_key, stream_info);
    if (route) {
      // Normalize the verb to ``spublish`` in place and forward the same array — no deep copy of
      // the channel and (possibly large) payload into a fresh request. This rewrites a client
      // ``PUBLISH`` (config-enabled) and byte-normalizes an explicit client ``SPUBLISH`` alike.
      // Force BulkString so the emitted verb is byte-identical regardless of how the client framed
      // the original verb element.
      incoming_request->asArray()[0].type(Common::Redis::RespType::BulkString);
      incoming_request->asArray()[0].asString() = Common::Redis::SupportedCommands::spublish();
    }
  } else {
    // Classic ``PUBLISH`` (client sent ``PUBLISH`` and sharded off — RESP2 listeners, Redis <7): it
    // is never delivered to proxy subscribers (SUBSCRIBE is always sharded), so there is no
    // hashing-parity constraint. Restore the pre-``PublishRequest`` ``simpleCommand`` behavior of
    // passing the request's channel element BY REFERENCE, so ``PrefixRoutes`` rewrites the upstream
    // wire request in place (``remove_prefix`` / ``key_formatter``). ``channel`` then reflects the
    // rewritten value for hashing too (both alias the same array element). (F4 regression fix.)
    route = router.upstreamPool(incoming_request->asArray()[1].asString(), stream_info);
  }
  if (route) {
    // Reuse the incoming array as the upstream request (unique -> shared, no payload copy).
    Common::Redis::RespValueSharedPtr base_request = std::move(incoming_request);
    // The upstream verb (``spublish`` when rewritten, else the original client verb) also drives
    // read/write pool selection and mirror policy in ``makeSingleServerRequest``
    // (``route->upstream`` / ``shouldMirror``): classic ``publish`` keeps its historical routing,
    // sharded ``spublish`` is write-pinned (see ``SupportedCommands::writeCommands``).
    const std::string& upstream_verb = base_request->asArray()[0].asString();
    request_ptr->handle_ = makeSingleServerRequest(route, upstream_verb, channel, base_request,
                                                   *request_ptr, callbacks.transaction());
  } else {
    ENVOY_LOG(debug, "route not found for publish: '{}'",
              incoming_request != nullptr ? incoming_request->toString() : std::string{});
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
  case Common::Redis::RespType::CompositeArray:
  case Common::Redis::RespType::Boolean:
  case Common::Redis::RespType::Double:
  case Common::Redis::RespType::BigNumber:
  case Common::Redis::RespType::VerbatimString:
  case Common::Redis::RespType::Map:
  case Common::Redis::RespType::Set:
  case Common::Redis::RespType::Push: {
    pending_response_->asArray()[index].type(Common::Redis::RespType::Error);
    pending_response_->asArray()[index].asString() = Response::get().UpstreamProtocolError;
    error_count_++;
    break;
  }
  case Common::Redis::RespType::BlobError:
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

  updateStats(!value->isError());
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

  updateStats(!value->isError());
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

SplitRequestPtr
TransactionRequest::create(Router& router, Common::Redis::RespValuePtr&& incoming_request,
                           SplitCallbacks& callbacks, CommandStats& command_stats,
                           TimeSource& time_source, bool delay_command_latency,
                           const StreamInfo::StreamInfo& stream_info,
                           const absl::flat_hash_set<std::string>& custom_commands) {
  Common::Redis::Client::Transaction& transaction = callbacks.transaction();
  std::string command_name = absl::AsciiStrToLower(incoming_request->asArray()[0].asString());

  // Within transactions we only support simple commands, including configured custom commands
  // which are treated as simple commands.
  // So if this is not a transaction command, a simple command or a custom command, it is an error.
  // We also support multi-key commands, but will leave it to the client to handle the case where
  // the keys provided are not from the same shard.
  //
  // PUBLISH and SPUBLISH are out of scope for the SUBSCRIBE→SSUBSCRIBE rewrite when issued
  // inside MULTI: they queue and execute through the transaction path untransformed (no
  // SPUBLISH upstream rewrite), preserving the pre-rewrite behavior for transactional
  // publishes documented in the changelog.
  const bool is_publish_family = command_name == Common::Redis::SupportedCommands::publish() ||
                                 command_name == Common::Redis::SupportedCommands::spublish();
  if (Common::Redis::SupportedCommands::transactionCommands().count(command_name) == 0 &&
      Common::Redis::SupportedCommands::simpleCommands().count(command_name) == 0 &&
      Common::Redis::SupportedCommands::multiKeyCommands().count(command_name) == 0 &&
      custom_commands.count(command_name) == 0 && !is_publish_family) {
    callbacks.onResponse(Common::Redis::Utility::makeError(
        fmt::format("'{}' command is not supported within transaction",
                    incoming_request->asArray()[0].asString())));
    return nullptr;
  }

  // publish/spublish were exempted from the splitter's generic ``size() < 2`` guard so
  // ``PublishRequest::create`` can own the arity error wording on the non-transaction
  // path. Inside MULTI the request goes through TransactionRequest instead, so the
  // arity check has to fire here — otherwise the transaction code path's
  // ``asArray()[1]`` access below would crash on bare ``PUBLISH`` inside MULTI.
  if (is_publish_family && incoming_request->asArray().size() != 3) {
    onWrongNumberOfArguments(callbacks, *incoming_request);
    command_stats.error_.inc();
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
        // Reserve one client for the main connection plus one per mirror policy.
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
      // Reserve one client for the main connection plus one per mirror policy.
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

SplitRequestPtr SubscriptionRequest::create(Router& router,
                                            Common::Redis::RespValuePtr&& incoming_request,
                                            SplitCallbacks& callbacks, CommandStats& command_stats,
                                            TimeSource& time_source, bool delay_command_latency,
                                            const StreamInfo::StreamInfo& stream_info) {
  // Subscription acks flow out-of-band, so this handler ALWAYS returns nullptr (never handed back
  // to the FIFO). Stack-allocate it and alias a raw pointer, so no per-SUBSCRIBE heap allocation is
  // made while every ``request_ptr->`` use below stays unchanged (E-7).
  SubscriptionRequest request(command_stats, time_source, delay_command_latency);
  SubscriptionRequest* request_ptr = &request;

  // This handler serves exactly SUBSCRIBE / UNSUBSCRIBE (subscriptionCommands()) — ``sunsubscribe``
  // (internal-only) and ``punsubscribe`` (unsupported) never reach it. Classify the raw verb
  // case-insensitively rather than allocating a lowercased copy (E-4 symmetry with PublishRequest),
  // and use the canonical lowercase form as ``command_name``: it is echoed as the downstream ack
  // verb (makeSubscriptionAck), and real Redis always replies lowercase regardless of the request's
  // case.
  static const std::string kSubscribeVerb = "subscribe";
  static const std::string kUnsubscribeVerb = "unsubscribe";
  const bool is_unsubscribe =
      absl::EqualsIgnoreCase(incoming_request->asArray()[0].asString(), kUnsubscribeVerb);
  const std::string& command_name = is_unsubscribe ? kUnsubscribeVerb : kSubscribeVerb;
  if (!is_unsubscribe && incoming_request->asArray().size() < 2) {
    onWrongNumberOfArguments(callbacks, *incoming_request);
    request_ptr->updateStats(false);
    return nullptr;
  }

  if (callbacks.currentDownstreamRespVersion() != 3) {
    // Sharded pub/sub delivers messages as RESP3 push frames, so the subscriber must be on RESP3.
    // Only tell the client to "Send HELLO 3 first" when that is actually achievable — i.e. the
    // LISTENER is configured for RESP3 and the client simply has not upgraded yet. When the
    // listener is NOT RESP3, HELLO 3 is answered with -NOPROTO (handleHelloCommand exact-matches
    // the listener's protocol_version), so that advice would loop the client between two errors and
    // never surface the real, operator-actionable condition. Report that pub/sub is unavailable on
    // this listener instead (SW-2), consistent with how a DISABLED subscription mode rejects the
    // command.
    const bool listener_is_resp3 =
        Common::Redis::toWireRespVersion(callbacks.protocolVersion()) == 3;
    callbacks.onResponse(Common::Redis::Utility::makeError(
        listener_is_resp3 ? "ERR pub/sub requires RESP3. Send HELLO 3 first."
                          : "ERR pub/sub is not enabled on this listener (RESP3 required)"));
    request_ptr->updateStats(false);
    return nullptr;
  }

  // Pub/sub state (subscriber, tracked registries, subscription-delta stats) lives on the
  // connection's PendingRequest, the sole PubsubSession. It is always present here: only a real
  // ProxyFilter dispatches subscribe/unsubscribe commands to this handler. Guard for real (not just
  // ASSERT): a future handler-registration change, a wrapper that does not forward pubsub(), or a
  // test double would otherwise null-deref in NDEBUG. Degrade to an inline -ERR instead. (F8)
  PubsubSession* pubsub = callbacks.pubsub();
  ASSERT(pubsub != nullptr);
  if (pubsub == nullptr) {
    callbacks.onResponse(Common::Redis::Utility::makeError("ERR pub/sub is not available"));
    request_ptr->updateStats(false);
    return nullptr;
  }

  DownstreamSubscriberPtr subscriber;
  const bool is_bare_unsubscribe = is_unsubscribe && incoming_request->asArray().size() < 2;
  // Only a bare ``UNSUBSCRIBE`` needs an owned snapshot: it enumerates the subscriber's current
  // channels, and the per-channel teardown below mutates ``subscribed_channels_`` as it drops each,
  // so iterating the live set would invalidate under us. Explicit channels are read straight off
  // the request array in the loop below (no per-channel string copy).
  std::vector<std::string> bare_unsubscribe_channels;
  if (is_bare_unsubscribe) {
    subscriber = pubsub->downstreamSubscriber();
    if (subscriber) {
      // Bare ``UNSUBSCRIBE`` enumerates the subscriber's current channels. After the
      // SUBSCRIBE→SSUBSCRIBE rewrite every subscribed channel lives in ``subscribed_channels_``
      // (the sole subscription set — cluster sharding is transparent), so read it to find what to
      // drop.
      const auto& active_subscriptions = subscriber->subscribedChannels();
      bare_unsubscribe_channels.assign(active_subscriptions.begin(), active_subscriptions.end());
    }

    if (bare_unsubscribe_channels.empty()) {
      // Deliver the bare-unsubscribe ack out-of-band via the subscriber, consistent with the
      // active-channel unsubscribe path below and with subscribe acks: every subscription-control
      // confirmation is a RESP3 Push frame delivered through subscriber->deliver(), never the
      // in-band FIFO. This lone path previously used the in-band onResponse(), so a bare
      // UNSUBSCRIBE's ack ordering relative to pipelined command replies flipped depending on
      // whether the subscriber happened to hold channels — the C-2 inconsistency. (A null
      // subscriber only happens once the connection is gone, in which case there is nothing to
      // ack; respond({}) still closes the request's FIFO entry with no frames.)
      //
      // respond({}) runs BEFORE the deliver: the ack's downstream write can synchronously drive a
      // connection close → ProxyFilter::onEvent destroys this PendingRequest (== callbacks), so
      // calling into callbacks after delivering would be a use-after-free. Complete the (empty)
      // FIFO entry first; the subscriber is owned by ProxyFilter and outlives the request, so
      // delivering afterward is safe.
      callbacks.respond({});
      if (subscriber) {
        subscriber->deliver(makeSubscriptionAck(command_name, nullptr, 0));
      }
      request_ptr->updateStats(true);
      return nullptr;
    }
  }

  if (!subscriber) {
    subscriber = pubsub->downstreamSubscriber();
  }
  if (!subscriber) {
    callbacks.onResponse(Common::Redis::Utility::makeError("ERR failed to create subscriber"));
    request_ptr->updateStats(false);
    return nullptr;
  }

  // Per-channel processing. The success/error stat fires once for the whole command after the
  // loop: we count it as success unless EVERY channel failed (matching Redis's
  // partial-success-with-inline-error semantics for multi-channel SUBSCRIBE).
  bool any_succeeded = false;
  bool any_failed = false;
  // Accumulate every per-channel inline -ERR here and hand the whole batch to the single terminal
  // respond() after the loop. Buffering (rather than emitting each error as it happens) preserves
  // the FIFO contract: the request stays one entry that flushes in order once complete.
  RespValueFrames frames;
  // Out-of-band subscribe/unsubscribe ack Push frames are ENCODED into the subscriber's reused
  // batch buffer as each channel is processed (appendAck), but not WRITTEN until AFTER the terminal
  // respond() below (flushAckBatch). Writing mid-loop would let a synchronous downstream close
  // (e.g. a write filter that closes on write) destroy this PendingRequest — which is what
  // ``callbacks`` /
  // ``pubsub`` alias — and leave the rest of the loop, and the terminal respond(), dereferencing
  // freed memory. Encoding mid-loop is side-effect-free, so only the write is deferred; the
  // registry guards its own fan-out the same way (deliverFrameToSubscribers snapshots first). The
  // subscriber is owned by ProxyFilter and outlives the request, so flushing after respond() is
  // safe.
  //
  // E-6: rather than build one ack RespValue tree per channel and hand a vector to deliverBatch, a
  // single ``ack_skeleton`` (``["<verb>", <channel>, <count>]`` Push) is reused across every
  // channel — only its channel string ([1]) and count ([2]) are mutated per iteration before
  // appendAck encodes it. ``command_name`` is invariant for the whole command (one verb, N
  // channels), so the skeleton's verb element ([0]) is correct throughout. ``preserved_acks`` is a
  // per-iteration scratch for the rare subscribe acks a shared-channel UNSUBSCRIBE must re-emit
  // (see unsubscribeChannelAcrossRegistries); it is cleared and reused each iteration, not
  // reallocated.
  std::vector<Common::Redis::RespValue> preserved_acks;
  const std::string skeleton_channel_placeholder;
  Common::Redis::RespValue ack_skeleton =
      makeSubscriptionAck(command_name, &skeleton_channel_placeholder, 0);
  // ``route_registry`` memoizes the per-Route registry resolution so a multi-channel SUBSCRIBE
  // whose channels share a prefix route resolves it once, not per channel (a null mapped value is
  // the "resolved, unavailable" sentinel). Unused on the UNSUBSCRIBE path (which routes via
  // trackedRegistries below). The pub/sub upstream itself is now resolved via
  // Route::pubsubUpstream() (A-3) rather than a hand-picked write verb string.
  absl::flat_hash_map<const Route*, SubscriptionRegistryPtr> route_registry;
  // Two per-channel snippets the subscribe and unsubscribe arms below both use, factored to lambdas
  // over the loop's shared state: emit the reused ack skeleton for a (channel, count), and report a
  // net per-subscriber delta to the cumulative subscribe/unsubscribe counters (the active-
  // subscriptions gauge already moved via add/removeChannel — A-2).
  const auto emit_ack = [&](const std::string& channel, uint64_t count) {
    ack_skeleton.asArray()[1].asString() = channel;
    ack_skeleton.asArray()[2].asInteger() = static_cast<int64_t>(count);
    subscriber->appendAck(ack_skeleton);
  };
  const auto report_subscription_delta = [&](uint64_t prev, uint64_t cur) {
    if (cur != prev) {
      pubsub->onPubsubSubscriptionChange(static_cast<int64_t>(cur) - static_cast<int64_t>(prev));
    }
  };
  // Process one channel (subscribe or unsubscribe arm). Invoked per channel from either iteration
  // source below — the bare-unsubscribe owned snapshot, or (for explicit channels) straight off the
  // request array with no per-channel copy. A per-channel skip is a ``return`` from this lambda.
  const auto process_channel = [&](const std::string& subscription_arg) {
    if (is_unsubscribe) {
      // Cross-registry unsubscribe is a session concern, not a routing one (A-8): the session walks
      // every registry that might own the channel (routing may have moved it since the SUBSCRIBE)
      // and buffers any preserved ``subscribe`` ack into ``preserved_acks``. Per-subscriber count
      // (not registry-wide distinct count) is what the ack and gauge track, so shared-channel
      // teardown stays correct when several subscribers hold the same channel on this thread.
      const uint64_t prev_subscriber_count = subscriber->totalSubscriptionCount();
      preserved_acks.clear();
      const uint64_t count =
          pubsub->unsubscribeChannelAcrossRegistries(subscription_arg, subscriber, preserved_acks);
      report_subscription_delta(prev_subscriber_count, subscriber->totalSubscriptionCount());
      // Emit any preserved subscribe acks first (rare shared-channel teardown), then this channel's
      // unsubscribe ack — matching the original preserved-then-unsub order, now via the reused
      // skeleton + encode-as-you-go instead of freshly built trees.
      for (const auto& preserved_ack : preserved_acks) {
        subscriber->appendAck(preserved_ack);
      }
      emit_ack(subscription_arg, count);
      any_succeeded = true;
      return;
    }

    // Subscribe path: route to the correct registry via upstreamPool. If routing or upstream
    // pub/sub is unavailable, deliver an inline error for *this* channel and mark the per-
    // channel failure — do NOT fake a success ack (that hid genuine failures from clients and
    // lied to the success stat).
    std::string route_key = subscription_arg;
    const auto route = router.upstreamPool(route_key, stream_info);
    if (!route) {
      ENVOY_LOG(warn, "redis: no route for pub/sub target '{}'", subscription_arg);
      // Per-channel -ERR buffered into ``frames`` (NOT terminal) so the FIFO entry stays alive for
      // the remaining channels in this multi-channel SUBSCRIBE and so the frame waits behind any
      // earlier pipelined non-pubsub request still pending upstream. The terminal respond() at the
      // bottom of the loop hands the whole batch over, marks the request done and triggers the
      // front-of-FIFO flush. Push frames + fabricated subscribe acks intentionally bypass the FIFO
      // via subscriber->deliver() — those are out-of-band per the RESP3 spec; ordinary -ERR replies
      // must NOT. (A subscribe-ack timeout / upstream SSUBSCRIBE error fires long after this
      // request already completed, so no FIFO entry is left to carry an error in band; rather than
      // write an unsolicited out-of-band -ERR — which a pipelining client would misattribute to an
      // earlier in-flight command — the registry rolls the subscription back and CLOSES the
      // subscriber's connection (F3). See SubscriptionRegistry::handleSubscribeAckTimeout.)
      frames.push_back(Common::Redis::Utility::makeError(
          fmt::format("ERR no route for pub/sub target '{}'", subscription_arg)));
      any_failed = true;
      return;
    }

    // Resolve the subscription registry for this route, memoized by Route*. ``upstreamPool``
    // above stays per-channel (prefix routing is genuinely key-dependent), but the registry
    // behind a given route is invariant — so a ``SUBSCRIBE c1..cN`` whose channels share a prefix
    // route resolves it (route->pubsubUpstream() + TLS deref + atomic load) once instead of N
    // times. A cached null is the valid "route resolved, pub/sub unavailable" sentinel (distinct
    // from an absent key), which collapses the two former failure causes — no write-side conn pool,
    // or no registry on the upstream — into the single unavailable error below.
    SubscriptionRegistryPtr registry;
    if (auto memo = route_registry.find(route.get()); memo != route_registry.end()) {
      registry = memo->second;
    } else {
      const auto upstream = route->pubsubUpstream();
      if (!upstream) {
        // Route matched but has no conn pool for the write-side verb (mirror-only or otherwise
        // partially-configured route). The spublish()-keyed lookup is new on this path, so a null
        // upstream that older key lookups never hit could surface here.
        ENVOY_LOG(warn, "redis: no upstream pool for pub/sub target '{}'", subscription_arg);
      } else {
        registry = upstream->subscriptionRegistryShared();
        if (!registry) {
          ENVOY_LOG(warn, "redis: pub/sub not available for target '{}'", subscription_arg);
        }
      }
      // Cache the outcome (registry or null) so later channels on this route skip re-resolution.
      route_registry.emplace(route.get(), registry);
      // Register the resolved registry with the subscriber once per route (idempotent) instead of
      // once per channel; the memo-hit path above reuses an already-registered registry.
      if (registry) {
        pubsub->setSubscriptionRegistry(registry);
      }
    }
    if (!registry) {
      // Route resolved but pub/sub is unavailable on it. Per-channel inline -ERR buffered into
      // ``frames`` (non-terminal; the terminal respond() at loop end flushes the FIFO — Push frames
      // and fabricated acks intentionally bypass the FIFO via subscriber->deliver(), ordinary -ERR
      // replies must not). The client-visible message is identical for both causes; the specific
      // cause was logged once above when this route was first resolved.
      frames.push_back(Common::Redis::Utility::makeError(
          fmt::format("ERR pub/sub unavailable on upstream for target '{}' (RESP3 not enabled)",
                      subscription_arg)));
      any_failed = true;
      return;
    }

    // Snapshot the pre-subscribe count for the subscribe/unsubscribe COUNTERS below. The
    // active-subscriptions GAUGE is not computed from this delta — addChannel owns it (A-2).
    const uint64_t prev_subscriber_count = subscriber->totalSubscriptionCount();
    // command_name is always "subscribe" on this branch: is_unsubscribe is false and the only
    // client-exposed subscribe-family verbs that reach this handler are subscribe / unsubscribe
    // (the sharded verbs are rejected earlier — see subscriptionCommands()). Assert the invariant
    // rather than branching to a terminal error that would discard any per-channel -ERR frames
    // already accumulated for earlier channels in this multi-channel SUBSCRIBE.
    ASSERT(command_name == "subscribe");
    // Rewrite the client SUBSCRIBE to a sharded SSUBSCRIBE. The registry always emits the
    // downstream ack with the literal ``subscribe`` verb, so the client still sees ``["subscribe",
    // channel, count]`` — cluster sharding is transparent. SUBSCRIBE is the only client-exposed
    // subscribe verb (PSUBSCRIBE is unsupported — see subscriptionCommands()).
    //
    // B-3 trade-off: a multi-channel ``SUBSCRIBE a b`` whose channels hash to DIFFERENT shards
    // issues one SSUBSCRIBE per shard, and each shard acks independently. So the downstream acks
    // can arrive out of command order — if b's shard acks first the client sees ``subscribe b 2``
    // before
    // ``subscribe a 1`` — and the trailing count is per-channel snapshot order, not the monotonic
    // 1,2,3,... a single-node Redis emits in strict command order. This is inherent to per-shard
    // fan-out (there is no cross-shard barrier to serialize the acks); a client that
    // position-matches acks to request order or asserts a monotonic count must not rely on it here.
    SubscriptionRegistry::SubscribeResult result =
        registry->subscribe(subscription_arg, subscriber);

    if (!result.success) {
      // Upstream send failed (no healthy host, conn-pool failure). Registry already rolled
      // back local state; surface as inline error rather than a fake success ack.
      ENVOY_LOG(warn, "redis: upstream {} send failed for '{}'", command_name, subscription_arg);
      // Buffered into ``frames``, not terminal (see no-route branch comment).
      frames.push_back(Common::Redis::Utility::makeError(
          fmt::format("ERR upstream {} send failed for '{}'", command_name, subscription_arg)));
      any_failed = true;
      return;
    }

    const uint64_t cur_subscriber_count = result.subscription_count;
    if (!result.ack_deferred) {
      // Dedup hit on an already-ACTIVE channel: its upstream ack has already landed, so no upstream
      // ack will fire on our behalf — fabricate the ack now to match Redis's per-client semantics
      // (encoded now via the reused skeleton, written after the terminal respond() below —
      // appendAck/flushAckBatch). A dedup on a
      // channel still awaiting its first upstream ack instead joins that pending bucket in
      // subscribe() (ack_deferred = true), so it waits for the upstream confirmation like the first
      // subscriber rather than seeing a premature success.
      emit_ack(subscription_arg, cur_subscriber_count);
    }
    // else: ack deferred to the registry; onPushMessage delivers it when the upstream subscribe-ack
    // Push arrives (fresh upstream send, or a dedup that joined the still-open pending bucket).
    report_subscription_delta(prev_subscriber_count, cur_subscriber_count);
    any_succeeded = true;
  };

  // Drive process_channel over the right source: a bare ``UNSUBSCRIBE`` walks its owned channel
  // snapshot; every other form (explicit SUBSCRIBE / UNSUBSCRIBE) reads the channel arguments in
  // place off the request array — no per-channel string copy.
  if (is_bare_unsubscribe) {
    for (const auto& channel : bare_unsubscribe_channels) {
      process_channel(channel);
    }
  } else {
    const auto& array = incoming_request->asArray();
    for (uint64_t i = 1; i < array.size(); ++i) {
      process_channel(array[i].asString());
    }
  }

  // Terminal: hand the accumulated per-channel error frames (possibly empty — all channels
  // succeeded with acks flowing out-of-band) to the request in one call, which marks it complete
  // and flushes the FIFO. This is the LAST use of ``callbacks`` / ``pubsub``, so a deliver-driven
  // synchronous close afterward can no longer dangle them.
  callbacks.respond(std::move(frames));
  // Flush the acks encoded during the loop now, in a SINGLE write: a bare ``UNSUBSCRIBE`` on N
  // channels accumulates N acks, and delivering them one-per-write was N downstream writes. The
  // subscriber is owned by ProxyFilter and outlives the (possibly just-popped) request;
  // flushAckBatch no-ops (draining any partial batch) if the connection has already closed and
  // never touches callbacks.
  subscriber->flushAckBatch();
  // Partial-success: count as command-level success unless every channel failed.
  request_ptr->updateStats(any_succeeded || !any_failed);
  return nullptr;
}

InstanceImpl::InstanceImpl(RouterPtr&& router, Stats::Scope& scope, const std::string& stat_prefix,
                           TimeSource& time_source, bool latency_in_micros,
                           Common::Redis::FaultManagerPtr&& fault_manager,
                           absl::flat_hash_set<std::string>&& custom_commands,
                           bool enable_sharded_subscribe)
    : router_(std::move(router)), simple_command_handler_(*router_), publish_handler_(*router_),
      eval_command_handler_(*router_), object_command_handler_(*router_), mget_handler_(*router_),
      mset_handler_(*router_), scan_handler_(*router_), shard_info_handler_(*router_),
      random_shard_handler_(*router_), split_keys_sum_result_handler_(*router_),
      custom_commands_(std::move(custom_commands)),
      transaction_handler_(*router_, custom_commands_), subscription_handler_(*router_),
      cluster_scope_handler_(*router_),
      stats_{ALL_COMMAND_SPLITTER_STATS(POOL_COUNTER_PREFIX(scope, stat_prefix + "splitter."))},
      time_source_(time_source), fault_manager_(std::move(fault_manager)),
      enable_sharded_subscribe_(enable_sharded_subscribe) {
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

  // PUBLISH and SPUBLISH share one handler. A client ``SPUBLISH`` always goes out as ``SPUBLISH``;
  // a client ``PUBLISH`` is rewritten to ``SPUBLISH`` only when ``enable_sharded_publish`` is set
  // (see PublishRequest::create), otherwise it is forwarded unchanged as classic ``PUBLISH``. Two
  // registrations keep per-verb stats isolated under ``command.publish.*`` / ``command.spublish.*``
  // so operators can tell which verb the client sent.
  // PublishRequest::create owns its arity check (so it emits the unified wrong-arg wording);
  // PUBLISH is otherwise a normal command (allowed in MULTI, subject to fault injection).
  addHandler(scope, stat_prefix, Common::Redis::SupportedCommands::publish(), latency_in_micros,
             publish_handler_, /*owns_arity_check=*/true);
  addHandler(scope, stat_prefix, Common::Redis::SupportedCommands::spublish(), latency_in_micros,
             publish_handler_, /*owns_arity_check=*/true);

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

  // The subscribe family owns its per-channel arity checks, is forbidden inside MULTI, and bypasses
  // fault injection (acks are out-of-band RESP3 Push frames). These capabilities live on the
  // handler registration so makeRequest() stays free of pub/sub command-name special cases. The
  // handlers are registered UNCONDITIONALLY even when sharded subscription is disabled (D2): the
  // disable is enforced in makeRequest, which rejects SUBSCRIBE/UNSUBSCRIBE as unknown commands
  // BEFORE the handler lookup. Registering anyway keeps that lookup's ``handler != nullptr``
  // invariant intact for the enabled path.
  for (const std::string& command : Common::Redis::SupportedCommands::subscriptionCommands()) {
    addHandler(scope, stat_prefix, command, latency_in_micros, subscription_handler_,
               /*owns_arity_check=*/true, /*forbidden_in_transaction=*/true,
               /*bypasses_fault_injection=*/true);
  }

  for (const std::string& command : Common::Redis::SupportedCommands::ClusterScopeCommands()) {
    addHandler(scope, stat_prefix, command, latency_in_micros, cluster_scope_handler_);
  }

  for (const std::string& command : custom_commands_) {
    // treating custom commands to be simple commands for now
    addHandler(scope, stat_prefix, command, latency_in_micros, simple_command_handler_);
  }

  {
    // See hello_command_stats_ in the header: locally-answered HELLO keeps the per-command
    // stats the old registration emitted.
    const std::string command_stat_prefix = fmt::format("{}command.hello.", stat_prefix);
    Stats::StatNameManagedStorage storage{command_stat_prefix + std::string("latency"),
                                          scope.symbolTable()};
    hello_command_stats_.emplace(CommandStats{
        ALL_COMMAND_STATS(POOL_COUNTER_PREFIX(scope, command_stat_prefix))
            scope.histogramFromStatName(storage.statName(),
                                        latency_in_micros ? Stats::Histogram::Unit::Microseconds
                                                          : Stats::Histogram::Unit::Milliseconds)});
  }
}

// HELLO is handled BEFORE the connectionAllowed gate so that ``HELLO N AUTH <user> <pass>`` can
// authenticate inline (RESP3 clients like lettuce/node-redis v4+/redis-py protocol=3 expect to
// authenticate via HELLO; running the auth gate first would NOAUTH every such first command). Bare
// HELLO still respects the gate; only HELLO with AUTH options bypasses it because the AUTH options
// are what authenticate the connection.
//
// ``HELLO N`` and bare HELLO (inherits ``currentDownstreamRespVersion()``) must match
// ``protocol_version`` exactly. Answered locally; the HELLO N AUTH + external auth path defers via
// ``attemptDownstreamAuthInline`` returning ImplOwnsResponse.
SplitRequestPtr InstanceImpl::handleHelloCommand(const Common::Redis::RespValue& request,
                                                 SplitCallbacks& callbacks) {
  const auto& args = request.asArray();
  const uint32_t required_version = Common::Redis::toWireRespVersion(callbacks.protocolVersion());
  uint32_t requested_version = callbacks.currentDownstreamRespVersion();
  bool explicit_version = false;
  std::string auth_user;
  std::string auth_pass;
  bool has_auth_option = false;

  // args[0] is "hello". args[1], if present, is the protover. args[2..] is
  // an optional sequence of option groups: AUTH <u> <p> | SETNAME <n>.
  size_t i = 1;
  if (i < args.size()) {
    const std::string& proto_arg = args[i].asString();
    int64_t proto_ver = 0;
    if (!absl::SimpleAtoi(proto_arg, &proto_ver) || proto_ver < 2 || proto_ver > 3) {
      hello_command_stats_->error_.inc();
      callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().UnsupportedProtocol));
      return nullptr;
    }
    requested_version = static_cast<uint32_t>(proto_ver);
    explicit_version = true;
    ++i;
  }

  // Exact-match: explicit N from args[1], bare HELLO from currentDownstreamRespVersion().
  if (requested_version != required_version) {
    hello_command_stats_->error_.inc();
    callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().UnsupportedProtocol));
    return nullptr;
  }

  // Parse remaining options. Only AUTH (3 tokens) and SETNAME (2 tokens)
  // are recognized; SETNAME is accepted and ignored because the proxy has
  // no per-client identity tracking. Anything else is a syntax error. A
  // repeated AUTH or SETNAME is rejected rather than silently last-wins;
  // this is stricter than real Redis (which processes repeated options in
  // order, so the last one wins) and keeps the proxy's handling of a
  // malformed handshake deterministic.
  bool seen_setname = false;
  while (i < args.size()) {
    const std::string opt = absl::AsciiStrToUpper(args[i].asString());
    if (opt == kHelloOptionAuth) {
      if (i + 2 >= args.size()) {
        hello_command_stats_->error_.inc();
        callbacks.onResponse(Common::Redis::Utility::makeError(
            "ERR Syntax error: HELLO AUTH requires <username> <password>"));
        return nullptr;
      }
      if (has_auth_option) {
        hello_command_stats_->error_.inc();
        callbacks.onResponse(Common::Redis::Utility::makeError(
            "ERR Syntax error: HELLO AUTH specified more than once"));
        return nullptr;
      }
      auth_user = args[i + 1].asString();
      auth_pass = args[i + 2].asString();
      has_auth_option = true;
      i += 3;
    } else if (opt == kHelloOptionSetname) {
      if (i + 1 >= args.size()) {
        hello_command_stats_->error_.inc();
        callbacks.onResponse(Common::Redis::Utility::makeError(
            "ERR Syntax error: HELLO SETNAME requires a <clientname>"));
        return nullptr;
      }
      if (seen_setname) {
        hello_command_stats_->error_.inc();
        callbacks.onResponse(Common::Redis::Utility::makeError(
            "ERR Syntax error: HELLO SETNAME specified more than once"));
        return nullptr;
      }
      seen_setname = true;
      // The ``HELLO N SETNAME <name>`` option is accepted but the name is intentionally
      // discarded — the proxy does not expose ``CLIENT LIST`` / ``CLIENT ID`` identity
      // state (those ``CLIENT`` subcommands are rejected elsewhere in this dispatcher;
      // their semantics over a multiplexed proxy are ambiguous), so there is no observable
      // consumer for the name. Accepted purely so RESP3 clients that include ``SETNAME``
      // in their ``HELLO`` handshake do not error out.
      i += 2;
    } else {
      hello_command_stats_->error_.inc();
      callbacks.onResponse(Common::Redis::Utility::makeError(
          fmt::format("ERR Syntax error: unknown HELLO option '{}'", args[i].asString())));
      return nullptr;
    }
  }

  if (has_auth_option) {
    using AuthAttempt = CommandSplitter::SplitCallbacks::AuthAttempt;
    switch (callbacks.attemptDownstreamAuthInline(auth_user, auth_pass, requested_version)) {
    case AuthAttempt::Allowed:
      // Fall through to HELLO reply emission below.
      break;
    case AuthAttempt::Denied:
      hello_command_stats_->error_.inc();
      callbacks.onResponse(
          Common::Redis::Utility::makeError("WRONGPASS invalid username-password pair"));
      return nullptr;
    case AuthAttempt::ImplOwnsResponse:
      // Outcome resolves inside the filter (deferred external auth or an already-emitted
      // synchronous error), so ``command.hello.success``/``error`` are intentionally NOT
      // incremented here — only ``total``. Wiring the deferred outcome back would need a
      // filter→splitter stats channel for a niche path whose result is already observable
      // through the external-auth provider's own signals and the downstream reply.
      // The implementation owns the response; the splitter emits nothing here and yields control.
      // Either external auth is in flight — the impl later emits the deferred HELLO reply (Map on
      // success, error on failure) plus the downstream RESP version flip, and ProxyFilter queues
      // any pipelined requests from this onData() pass via external_auth_call_status_ until that
      // round trip resolves — or the impl already emitted a final error synchronously (HELLO AUTH
      // with no downstream credentials configured), in which case nothing is pending.
      return nullptr;
    }
  } else if (!callbacks.connectionAllowed()) {
    // Bare HELLO (no AUTH option) still requires prior authentication on
    // an auth-required listener.
    hello_command_stats_->error_.inc();
    callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().AuthRequiredError));
    return nullptr;
  }

  // Flip the downstream encoder first so the reply is encoded in the newly
  // negotiated version. For bare HELLO, this is a no-op (same version).
  if (explicit_version) {
    callbacks.setDownstreamRespVersion(requested_version);
  }
  hello_command_stats_->success_.inc();
  callbacks.onResponse(buildHelloReply(requested_version));
  return nullptr;
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

  // Hard-reject the sharded pub/sub commands that the proxy intentionally does not
  // expose to clients. SUBSCRIBE/UNSUBSCRIBE are silently rewritten into their sharded
  // variants (SSUBSCRIBE/SUNSUBSCRIBE) on the upstream wire — clients have no business
  // issuing the s-variants directly. The check runs BEFORE the ``custom_commands_`` lookup
  // so an operator config of ``custom_commands: [ssubscribe]`` cannot accidentally
  // re-enable them. SPUBLISH is intentionally NOT on this list: unlike the subscribe
  // s-variants it IS client-exposed — a sharded publish that shares the PUBLISH handler and
  // is always forwarded upstream as SPUBLISH (a plain PUBLISH is rewritten to SPUBLISH only
  // when ``enable_sharded_publish`` is set).
  if (command_name == "ssubscribe" || command_name == "sunsubscribe") {
    rejectUnknownCommand(callbacks, *request);
    return nullptr;
  }

  // D2: when sharded subscription is disabled (PubsubSettings mode DISABLED — the Redis 6.x escape
  // hatch), reject client SUBSCRIBE/UNSUBSCRIBE as unknown commands too, ahead of auth exactly like
  // PSUBSCRIBE and the internal sharded verbs above. The client gets a clean ``-ERR unknown
  // command`` (the pre-sharded-pubsub behavior) rather than the proxy rewriting it into an
  // SSUBSCRIBE the upstream answers with ``-ERR unknown command``, which would churn the
  // subscription connection through backoff until it closes the client.
  if (!enable_sharded_subscribe_ &&
      Common::Redis::SupportedCommands::subscriptionCommands().contains(command_name)) {
    rejectUnknownCommand(callbacks, *request);
    return nullptr;
  }

  // Compatible with redis behavior, if there is an unsupported command, return immediately,
  // this action must be performed before verifying auth, some redis clients rely on this behavior.
  if (!Common::Redis::SupportedCommands::isSupportedCommand(command_name) &&
      custom_commands_.find(command_name) == custom_commands_.end()) {
    rejectUnknownCommand(callbacks, *request);
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

  // HELLO has its own gate with inline-AUTH support; see InstanceImpl::handleHelloCommand. It runs
  // before the connectionAllowed gate so a ``HELLO N AUTH ...`` can authenticate inline. Inside an
  // active MULTI it is rejected with the transaction allowlist's error shape instead — real Redis
  // likewise refuses HELLO in a transaction, and answering it locally here would flip the
  // protocol mid-transaction and emit a reply EXEC never accounts for.
  if (command_name == Common::Redis::SupportedCommands::hello()) {
    hello_command_stats_->total_.inc();
    if (callbacks.transaction().active_) {
      hello_command_stats_->error_.inc();
      callbacks.onResponse(Common::Redis::Utility::makeError(
          fmt::format("'{}' command is not supported within transaction", command_name)));
      return nullptr;
    }
    return handleHelloCommand(*request, callbacks);
  }

  // QUIT bypasses the auth gate (matches real Redis — clients can always close gracefully).
  if (command_name == Common::Redis::SupportedCommands::quit()) {
    callbacks.onQuit();
    return nullptr;
  }

  // Auth gate for non-HELLO/QUIT commands. HELLO has its own gate above (with
  // inline-AUTH support); all other commands require prior authentication
  // when the listener is auth-required.
  if (!callbacks.connectionAllowed()) {
    callbacks.onResponse(Common::Redis::Utility::makeError(Response::get().AuthRequiredError));
    return nullptr;
  }

  // CLIENT command: only the ``SETINFO`` and ``SETNAME`` subcommands are recognized.
  // Modern RESP3 clients (node-redis v4+, redis-py protocol=3) issue these
  // automatically on connection setup to record library/version metadata for
  // server-side CLIENT LIST diagnostics. Without local handling they would
  // hit the unsupported-command path above and break client startup. The
  // proxy has no client-identity tracking, so we accept and reply +OK,
  // discarding the supplied metadata. Other CLIENT subcommands (LIST, KILL,
  // ID, NO-EVICT, ...) are deliberately rejected: they expose or mutate
  // upstream connection state, which would be ambiguous over a multiplexed
  // proxy connection. Inside an active MULTI the local +OK shortcut is rejected with the
  // transaction allowlist's error shape — an out-of-band +OK here would desynchronize EXEC's reply
  // count. (Real Redis queues CLIENT in a transaction; the proxy's transaction model only
  // forwards allowlisted data commands.)
  if (command_name == Common::Redis::SupportedCommands::client() &&
      custom_commands_.find(command_name) == custom_commands_.end()) {
    // Deployments that predate local CLIENT handling may proxy CLIENT upstream via
    // ``custom_commands``; that explicit operator opt-in wins over the proxy's local
    // interception of the ``SETNAME`` and ``SETINFO`` subcommands — the generic handler
    // lookup below then routes CLIENT like any simple command.
    if (callbacks.transaction().active_) {
      callbacks.onResponse(Common::Redis::Utility::makeError(
          fmt::format("'{}' command is not supported within transaction", command_name)));
      return nullptr;
    }
    if (request->asArray().size() < 2) {
      onInvalidRequest(callbacks);
      return nullptr;
    }
    const std::string subcommand = absl::AsciiStrToLower(request->asArray()[1].asString());
    // ``CLIENT SETNAME <name>`` (3 tokens) and ``CLIENT SETINFO <attr> <value>`` (4 tokens)
    // are accepted and discarded. Enforce the exact token count so a malformed handshake gets
    // a syntax error rather than a misleading +OK.
    if (subcommand == "setname" || subcommand == "setinfo") {
      const size_t expected = subcommand == "setname" ? 3 : 4;
      if (request->asArray().size() != expected) {
        callbacks.onResponse(Common::Redis::Utility::makeError(
            fmt::format("ERR wrong number of arguments for 'client|{}' command", subcommand)));
        return nullptr;
      }
      localResponse(callbacks, "OK");
      return nullptr;
    }
    callbacks.onResponse(Common::Redis::Utility::makeError(
        fmt::format("ERR CLIENT subcommand '{}' is not supported by the proxy",
                    request->asArray()[1].asString())));
    return nullptr;
  }

  if (command_name == Common::Redis::SupportedCommands::ping()) {
    // Respond to PING locally.
    localResponse(callbacks, "PONG");
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

  // Get the handler for the downstream request. Unsupported commands were rejected above and the
  // locally-answered commands (PING/ECHO/TIME/...) returned earlier, so every command reaching here
  // has a registered handler — its capability flags now drive the dispatch decisions below.
  auto handler = handler_lookup_table_.find(command_name);
  ASSERT(handler != nullptr);

  if (request->asArray().size() < 2 &&
      !Common::Redis::SupportedCommands::isCommandValidWithoutArgs(command_name) &&
      !handler->owns_arity_check) {
    // Commands that require at least one argument beyond the command name. Handlers that own their
    // arity validation (PublishRequest, the subscribe family) opt out via ``owns_arity_check`` so
    // they can emit the unified ``wrong number of arguments for '<cmd>' command`` wording from
    // ``SplitRequestBase::onWrongNumberOfArguments``.
    onInvalidRequest(callbacks);
    return nullptr;
  }

  // If we are within a transaction, forward all requests to the transaction handler (i.e. handler
  // of "multi" command) — except pub/sub, which real Redis forbids inside MULTI. Reject those
  // here rather than letting them fall through to the transaction handler: routed there, a
  // SUBSCRIBE would be sent raw to a single shard with no downstream subscriber / registry / RESP3
  // gate, and its RESP3 Push subscribe-ack would be dropped by ClientImpl (no push_callbacks_ on a
  // data connection), hanging the request until op-timeout. The guard runs before the multi
  // reassignment so the subscription fast path below still sees the real subscription handler
  // outside transactions.
  if (callbacks.transaction().active_) {
    if (handler->forbidden_in_transaction) {
      callbacks.onResponse(Common::Redis::Utility::makeError(
          fmt::format("{} is not allowed in transactions", absl::AsciiStrToUpper(command_name))));
      return nullptr;
    }
    handler = handler_lookup_table_.find("multi");
  }

  // Subscription commands intentionally bypass the fault-injection layer below.
  //
  // Delay/error faults only make sense for a command whose entire reply flows through the fault
  // wrapper. A subscribe-family command's output is split: successful subscribe/unsubscribe acks
  // are RESP3 Push frames delivered out-of-band via subscriber->deliver() (they never touch the
  // wrapper), and only the per-channel inline ``-ERR`` replies go through respond(). Wrapping such
  // a command in DelayFaultRequest would fire the acks immediately but delay just the error replies
  // — a meaningless, inconsistent "delay" — so pub/sub is pinned out of fault injection by design.
  //
  // (This is not a lifetime constraint. DelayFaultRequest::respond() defers, and completing the
  // wrapped PendingRequest from the timer callback destroys the DelayFaultRequest — and its own
  // timer — mid-callback; but that reentrant teardown is the same path every delay-faulted command
  // takes and is safe because the wrapper's callback returns immediately afterward. The former
  // second reason — that DelayFaultRequest could not carry a multi-frame response — also no longer
  // applies now that respond() takes the whole frame vector.)
  //
  // The intentional skip means delay/error fault injection does not apply to ``SUBSCRIBE`` /
  // ``PSUBSCRIBE`` / ``SSUBSCRIBE`` / ``UNSUBSCRIBE`` / ``PUNSUBSCRIBE`` / ``SUNSUBSCRIBE``;
  // covered by tests pinning that pub/sub paths bypass the fault manager.
  if (handler->bypasses_fault_injection) {
    ENVOY_LOG(debug, "splitting '{}'", request->toString());
    handler->command_stats_.total_.inc();
    return handler->handler_.get().startRequest(
        std::move(request), callbacks, handler->command_stats_, time_source_, false, stream_info);
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

void InstanceImpl::rejectUnknownCommand(SplitCallbacks& callbacks,
                                        const Common::Redis::RespValue& request) {
  stats_.unsupported_command_.inc();
  callbacks.onResponse(Common::Redis::Utility::makeError(fmt::format(
      "ERR unknown command '{}', with args beginning with: {}", request.asArray()[0].asString(),
      request.asArray().size() > 1 ? request.asArray()[1].asString() : "")));
}

void InstanceImpl::addHandler(Stats::Scope& scope, const std::string& stat_prefix,
                              const std::string& name, bool latency_in_micros,
                              CommandHandler& handler, bool owns_arity_check,
                              bool forbidden_in_transaction, bool bypasses_fault_injection) {
  std::string to_lower_name = absl::AsciiStrToLower(name);
  const std::string command_stat_prefix = fmt::format("{}command.{}.", stat_prefix, to_lower_name);
  Stats::StatNameManagedStorage storage{command_stat_prefix + std::string("latency"),
                                        scope.symbolTable()};
  handler_lookup_table_.add(
      to_lower_name,
      std::make_shared<HandlerData>(HandlerData{
          CommandStats{ALL_COMMAND_STATS(POOL_COUNTER_PREFIX(scope, command_stat_prefix))
                           scope.histogramFromStatName(storage.statName(),
                                                       latency_in_micros
                                                           ? Stats::Histogram::Unit::Microseconds
                                                           : Stats::Histogram::Unit::Milliseconds)},
          handler, owns_arity_check, forbidden_in_transaction, bypasses_fault_injection}));
}

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
