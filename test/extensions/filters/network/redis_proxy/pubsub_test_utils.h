#pragma once

#include <memory>
#include <string>
#include <vector>

#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/network/common/redis/codec.h"
#include "source/extensions/filters/network/redis_proxy/subscription_registry.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

// Shared throwaway pub/sub subscriber stats for tests that construct a DownstreamSubscriber but do
// not assert on its telemetry (S-4 made the four stats mandatory). Backed by a function-local
// IsolatedStore so every caller shares one set of counters and no test needs to plumb its own.
inline DownstreamSubscriberStats& testDownstreamSubscriberStats() {
  static Stats::IsolatedStoreImpl store;
  static DownstreamSubscriberStats stats{
      store.rootScope()->counterFromString("test.pubsub.push_delivered"),
      store.rootScope()->gaugeFromString("test.pubsub.active_subscriptions",
                                         Stats::Gauge::ImportMode::Accumulate),
      store.rootScope()->counterFromString("test.pubsub.subscribe_ack_success"),
      store.rootScope()->counterFromString("test.pubsub.subscribe_ack_error")};
  return stats;
}

// Shared builders for the RESP3 Push frames exercised by the pub/sub tests, so the ``smessage`` and
// subscribe-ack frame shapes have a single source of truth across subscription_registry_test and
// command_splitter_impl_test.

// A three-BulkString message Push ``[verb, channel, data]``. ``verb`` is ``smessage`` for the
// upstream per-shard frame the registry normalizes, or ``message`` for the downstream frame the
// registry/filter emit — so both wire shapes come from one builder.
inline Common::Redis::RespValuePtr
makeMessagePush(const std::string& verb, const std::string& channel, const std::string& data) {
  auto value = std::make_unique<Common::Redis::RespValue>();
  value->type(Common::Redis::RespType::Push);
  std::vector<Common::Redis::RespValue> array(3);
  array[0].type(Common::Redis::RespType::BulkString);
  array[0].asString() = verb;
  array[1].type(Common::Redis::RespType::BulkString);
  array[1].asString() = channel;
  array[2].type(Common::Redis::RespType::BulkString);
  array[2].asString() = data;
  value->asArray().swap(array);
  return value;
}

// Upstream per-shard message Push ``[smessage, channel, data]``. The registry normalizes the
// ``smessage`` verb to ``message`` before downstream delivery.
inline Common::Redis::RespValuePtr makeSmessagePush(const std::string& channel,
                                                    const std::string& data) {
  return makeMessagePush("smessage", channel, data);
}

// Upstream subscribe-ack Push ``[verb, channel, count]`` (verb/channel BulkStrings, count Integer).
// ``upstream_count`` is deliberately arbitrary in tests that assert the downstream ack count: the
// registry emits the per-subscriber snapshot captured at subscribe-call time, not the count echoed
// here by the upstream.
inline Common::Redis::RespValuePtr
makeSubscribeAckPush(const std::string& verb, const std::string& channel, int64_t upstream_count) {
  auto value = std::make_unique<Common::Redis::RespValue>();
  value->type(Common::Redis::RespType::Push);
  std::vector<Common::Redis::RespValue> array(3);
  array[0].type(Common::Redis::RespType::BulkString);
  array[0].asString() = verb;
  array[1].type(Common::Redis::RespType::BulkString);
  array[1].asString() = channel;
  array[2].type(Common::Redis::RespType::Integer);
  array[2].asInteger() = upstream_count;
  value->asArray().swap(array);
  return value;
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
