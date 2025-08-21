#pragma once

#include <sys/types.h>

#include "envoy/buffer/buffer.h"
#include "envoy/common/backoff_strategy.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/backoff_strategy.h"
#include "source/common/common/logger.h"

#include "msgpack.hpp"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Fluentd {

static constexpr uint64_t DefaultBaseBackoffIntervalMs = 500;
static constexpr uint64_t DefaultMaxBackoffIntervalFactor = 10;
static constexpr uint64_t DefaultBufferFlushIntervalMs = 1000;
static constexpr uint64_t DefaultMaxBufferSize = 16384;

using MessagePackBuffer = msgpack::sbuffer;

// Entry represents a single Fluentd message, msgpack format based, as specified in:
// https://github.com/fluent/fluentd/wiki/Forward-Protocol-Specification-v1#entry
class Entry {
public:
  Entry(const Entry&) = delete;
  Entry& operator=(const Entry&) = delete;
  Entry(uint64_t time, std::vector<uint8_t>&& record) : time_(time), record_(record) {}
  Entry(uint64_t time, std::map<std::string, std::string>&& map_record)
      : time_(time), map_record_(map_record) {}

  const uint64_t time_;
  const std::vector<uint8_t> record_;
  const std::map<std::string, std::string> map_record_;
};

using EntryPtr = std::unique_ptr<Entry>;

#define FLUENTD_STATS(COUNTER, GAUGE)                                                              \
  COUNTER(entries_lost)                                                                            \
  COUNTER(entries_buffered)                                                                        \
  COUNTER(events_sent)                                                                             \
  COUNTER(reconnect_attempts)                                                                      \
  COUNTER(connections_closed)

struct FluentdStats {
  FLUENTD_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

class FluentdService {
public:
  virtual ~FluentdService() = default;

  /**
   * Send the Fluentd formatted message over the upstream TCP connection.
   */
  virtual void log(EntryPtr&& entry) PURE;
};

using MessagePackBuffer = msgpack::sbuffer;
using MessagePackPacker = msgpack::packer<msgpack::sbuffer>;

class FluentdBase : public Tcp::AsyncTcpClientCallbacks,
                    public FluentdService,
                    public Logger::Loggable<Logger::Id::client> {
public:
  FluentdBase(Upstream::ThreadLocalCluster& cluster, Tcp::AsyncTcpClientPtr client,
              Event::Dispatcher& dispatcher, const std::string& tag,
              absl::optional<uint32_t> max_connect_attempts, Stats::Scope& parent_scope,
              const std::string& stat_prefix, BackOffStrategyPtr backoff_strategy,
              uint64_t buffer_flush_interval, uint64_t max_buffer_size);

  virtual ~FluentdBase() = default;

  // Tcp::AsyncTcpClientCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}
  void onData(Buffer::Instance&, bool) override {}

  // FluentdService
  void log(EntryPtr&& entry) override;

protected:
  void flush();
  void connect();
  void maybeReconnect();
  void onBackoffCallback();
  void setDisconnected();
  void clearBuffer();
  virtual void packMessage(MessagePackPacker& packer) = 0;

  bool disconnected_ = false;
  bool connecting_ = false;
  std::string tag_;
  std::string id_;
  uint32_t connect_attempts_{0};
  absl::optional<uint32_t> max_connect_attempts_;
  const Stats::ScopeSharedPtr stats_scope_;
  Upstream::ThreadLocalCluster& cluster_;
  const BackOffStrategyPtr backoff_strategy_;
  const Tcp::AsyncTcpClientPtr client_;
  const std::chrono::milliseconds buffer_flush_interval_msec_;
  const uint64_t max_buffer_size_bytes_;
  const Event::TimerPtr retry_timer_;
  const Event::TimerPtr flush_timer_;
  FluentdStats fluentd_stats_;
  std::vector<EntryPtr> entries_;
  uint64_t approximate_message_size_bytes_ = 0;
};

template <typename ConfigType, typename SharedPtrType> class FluentdCache {
public:
  virtual ~FluentdCache() = default;

  virtual SharedPtrType getOrCreate(const std::shared_ptr<ConfigType>& config,
                                    Random::RandomGenerator& random,
                                    BackOffStrategyPtr backoff_strategy) = 0;
};

template <typename T, typename ConfigType, typename SharedPtrType, typename WeakPtrType>
class FluentdCacheBase : public FluentdCache<ConfigType, SharedPtrType> {
public:
  FluentdCacheBase(Upstream::ClusterManager& cluster_manager, Stats::Scope& parent_scope,
                   ThreadLocal::SlotAllocator& tls, const std::string& scope_prefix)
      : cluster_manager_(cluster_manager), stats_scope_(parent_scope.createScope(scope_prefix)),
        tls_slot_(tls.allocateSlot()) {
    tls_slot_->set([](Event::Dispatcher& dispatcher) {
      return std::make_shared<ThreadLocalCache>(dispatcher);
    });
  }

  virtual ~FluentdCacheBase() = default;

  SharedPtrType getOrCreate(const std::shared_ptr<ConfigType>& config,
                            Random::RandomGenerator& random,
                            BackOffStrategyPtr backoff_strategy) override {
    auto& cache = tls_slot_->getTyped<ThreadLocalCache>();
    const auto cache_key = MessageUtil::hash(*config);
    auto it = cache.instances_.find(cache_key);
    if (it != cache.instances_.end() && !it->second.expired()) {
      return it->second.lock();
    }

    auto* cluster = cluster_manager_.getThreadLocalCluster(config->cluster());
    if (!cluster) {
      return nullptr;
    }

    auto client =
        cluster->tcpAsyncClient(nullptr, std::make_shared<const Tcp::AsyncTcpClientOptions>(false));

    auto instance = createInstance(*cluster, std::move(client), cache.dispatcher_, *config,
                                   std::move(backoff_strategy), random);
    cache.instances_.emplace(cache_key, instance);
    return instance;
  }

protected:
  virtual SharedPtrType createInstance(Upstream::ThreadLocalCluster& cluster,
                                       Tcp::AsyncTcpClientPtr client, Event::Dispatcher& dispatcher,
                                       const ConfigType& config,
                                       BackOffStrategyPtr backoff_strategy,
                                       Random::RandomGenerator& random) = 0;

  struct ThreadLocalCache : public ThreadLocal::ThreadLocalObject {
    ThreadLocalCache(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

    Event::Dispatcher& dispatcher_;
    // Tracers indexed by the hash of tracer's configuration.
    absl::flat_hash_map<std::size_t, WeakPtrType> instances_;
  };

  Upstream::ClusterManager& cluster_manager_;
  Stats::ScopeSharedPtr stats_scope_;
  ThreadLocal::SlotPtr tls_slot_;
};

} // namespace Fluentd
} // namespace Common
} // namespace Extensions
} // namespace Envoy
