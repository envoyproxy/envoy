#include "source/extensions/access_loggers/fluentd/fluentd_access_log_impl.h"

#include "source/common/buffer/buffer_impl.h"

#include "msgpack.hpp"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Fluentd {

using MessagePackBuffer = msgpack::sbuffer;
using MessagePackPacker = msgpack::packer<msgpack::sbuffer>;

FluentdAccessLoggerImpl::FluentdAccessLoggerImpl(Upstream::ThreadLocalCluster& cluster,
                                                 Tcp::AsyncTcpClientPtr client,
                                                 Event::Dispatcher& dispatcher,
                                                 const FluentdAccessLogConfig& config,
                                                 BackOffStrategyPtr backoff_strategy,
                                                 Stats::Scope& parent_scope)
    : tag_(config.tag()), id_(dispatcher.name()),
      max_connect_attempts_(
          config.has_retry_options() && config.retry_options().has_max_connect_attempts()
              ? absl::optional<uint32_t>(config.retry_options().max_connect_attempts().value())
              : absl::nullopt),
      stats_scope_(parent_scope.createScope(config.stat_prefix())),
      fluentd_stats_(
          {ACCESS_LOG_FLUENTD_STATS(POOL_COUNTER(*stats_scope_), POOL_GAUGE(*stats_scope_))}),
      cluster_(cluster), backoff_strategy_(std::move(backoff_strategy)), client_(std::move(client)),
      buffer_flush_interval_msec_(
          PROTOBUF_GET_MS_OR_DEFAULT(config, buffer_flush_interval, DefaultBufferFlushIntervalMs)),
      max_buffer_size_bytes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, buffer_size_bytes, DefaultMaxBufferSize)),
      retry_timer_(dispatcher.createTimer([this]() -> void { onBackoffCallback(); })),
      flush_timer_(dispatcher.createTimer([this]() {
        flush();
        flush_timer_->enableTimer(buffer_flush_interval_msec_);
      })) {
  client_->setAsyncTcpClientCallbacks(*this);
  flush_timer_->enableTimer(buffer_flush_interval_msec_);
}

void FluentdAccessLoggerImpl::onEvent(Network::ConnectionEvent event) {
  connecting_ = false;

  if (event == Network::ConnectionEvent::Connected) {
    backoff_strategy_->reset();
    retry_timer_->disableTimer();
    flush();
  } else if (event == Network::ConnectionEvent::LocalClose ||
             event == Network::ConnectionEvent::RemoteClose) {
    ENVOY_LOG(debug, "upstream connection was closed");
    fluentd_stats_.connections_closed_.inc();
    maybeReconnect();
  }
}

void FluentdAccessLoggerImpl::log(EntryPtr&& entry) {
  if (disconnected_ || approximate_message_size_bytes_ >= max_buffer_size_bytes_) {
    fluentd_stats_.entries_lost_.inc();
    // We will lose the data deliberately so the buffer doesn't grow infinitely.
    return;
  }

  approximate_message_size_bytes_ += sizeof(entry->time_) + entry->record_.size();
  entries_.push_back(std::move(entry));
  fluentd_stats_.entries_buffered_.inc();
  if (approximate_message_size_bytes_ >= max_buffer_size_bytes_) {
    // If we exceeded the buffer limit, immediately flush the logs instead of waiting for
    // the next flush interval, to allow new logs to be buffered.
    flush();
  }
}

void FluentdAccessLoggerImpl::flush() {
  ASSERT(!disconnected_);

  if (entries_.size() == 0 || connecting_) {
    // nothing to send, or we're still waiting for an upstream connection.
    return;
  }

  if (!client_->connected()) {
    connect();
    return;
  }

  // Creating a Fluentd Forward Protocol Specification (v1) forward mode event as specified in:
  // https://github.com/fluent/fluentd/wiki/Forward-Protocol-Specification-v1#forward-mode
  MessagePackBuffer buffer;
  MessagePackPacker packer(buffer);
  packer.pack_array(2); // 1 - tag field, 2 - entries array.
  packer.pack(tag_);
  packer.pack_array(entries_.size());

  for (auto& entry : entries_) {
    packer.pack_array(2); // 1 - time, 2 - record.
    packer.pack(entry->time_);
    const char* record_bytes = reinterpret_cast<const char*>(&entry->record_[0]);
    packer.pack_bin_body(record_bytes, entry->record_.size());
  }

  Buffer::OwnedImpl data(buffer.data(), buffer.size());
  client_->write(data, false);
  fluentd_stats_.events_sent_.inc();
  clearBuffer();
}

void FluentdAccessLoggerImpl::connect() {
  connect_attempts_++;
  if (!client_->connect()) {
    ENVOY_LOG(debug, "no healthy upstream");
    maybeReconnect();
    return;
  }

  connecting_ = true;
}

void FluentdAccessLoggerImpl::maybeReconnect() {
  if (max_connect_attempts_.has_value() && connect_attempts_ >= max_connect_attempts_) {
    ENVOY_LOG(debug, "max connection attempts reached");
    cluster_.info()->trafficStats()->upstream_cx_connect_attempts_exceeded_.inc();
    setDisconnected();
    return;
  }

  uint64_t next_backoff_ms = backoff_strategy_->nextBackOffMs();
  retry_timer_->enableTimer(std::chrono::milliseconds(next_backoff_ms));
  ENVOY_LOG(debug, "reconnect attempt scheduled for {} ms", next_backoff_ms);
}

void FluentdAccessLoggerImpl::onBackoffCallback() {
  fluentd_stats_.reconnect_attempts_.inc();
  this->connect();
}

void FluentdAccessLoggerImpl::setDisconnected() {
  disconnected_ = true;
  clearBuffer();
  ASSERT(flush_timer_ != nullptr);
  flush_timer_->disableTimer();
}

void FluentdAccessLoggerImpl::clearBuffer() {
  entries_.clear();
  approximate_message_size_bytes_ = 0;
}

FluentdAccessLoggerCacheImpl::FluentdAccessLoggerCacheImpl(
    Upstream::ClusterManager& cluster_manager, Stats::Scope& parent_scope,
    ThreadLocal::SlotAllocator& tls)
    : cluster_manager_(cluster_manager),
      stats_scope_(parent_scope.createScope("access_logs.fluentd")), tls_slot_(tls.allocateSlot()) {
  tls_slot_->set(
      [](Event::Dispatcher& dispatcher) { return std::make_shared<ThreadLocalCache>(dispatcher); });
}

FluentdAccessLoggerSharedPtr
FluentdAccessLoggerCacheImpl::getOrCreateLogger(const FluentdAccessLogConfigSharedPtr config,
                                                Random::RandomGenerator& random) {
  auto& cache = tls_slot_->getTyped<ThreadLocalCache>();
  const auto cache_key = MessageUtil::hash(*config);
  const auto it = cache.access_loggers_.find(cache_key);
  if (it != cache.access_loggers_.end() && !it->second.expired()) {
    return it->second.lock();
  }

  auto* cluster = cluster_manager_.getThreadLocalCluster(config->cluster());
  if (!cluster) {
    return nullptr;
  }

  auto client =
      cluster->tcpAsyncClient(nullptr, std::make_shared<const Tcp::AsyncTcpClientOptions>(false));

  uint64_t base_interval_ms = DefaultBaseBackoffIntervalMs;
  uint64_t max_interval_ms = base_interval_ms * DefaultMaxBackoffIntervalFactor;

  if (config->has_retry_options() && config->retry_options().has_backoff_options()) {
    base_interval_ms = PROTOBUF_GET_MS_OR_DEFAULT(config->retry_options().backoff_options(),
                                                  base_interval, DefaultBaseBackoffIntervalMs);
    max_interval_ms =
        PROTOBUF_GET_MS_OR_DEFAULT(config->retry_options().backoff_options(), max_interval,
                                   base_interval_ms * DefaultMaxBackoffIntervalFactor);
  }

  BackOffStrategyPtr backoff_strategy = std::make_unique<JitteredExponentialBackOffStrategy>(
      base_interval_ms, max_interval_ms, random);

  const auto logger = std::make_shared<FluentdAccessLoggerImpl>(
      *cluster, std::move(client), cache.dispatcher_, *config, std::move(backoff_strategy),
      *stats_scope_);
  cache.access_loggers_.emplace(cache_key, logger);
  return logger;
}

FluentdAccessLog::FluentdAccessLog(AccessLog::FilterPtr&& filter, FluentdFormatterPtr&& formatter,
                                   const FluentdAccessLogConfigSharedPtr config,
                                   ThreadLocal::SlotAllocator& tls, Random::RandomGenerator& random,
                                   FluentdAccessLoggerCacheSharedPtr access_logger_cache)
    : ImplBase(std::move(filter)), formatter_(std::move(formatter)), tls_slot_(tls.allocateSlot()),
      config_(config), access_logger_cache_(access_logger_cache) {
  tls_slot_->set(
      [config = config_, &random, access_logger_cache = access_logger_cache_](Event::Dispatcher&) {
        return std::make_shared<ThreadLocalLogger>(
            access_logger_cache->getOrCreateLogger(config, random));
      });
}

void FluentdAccessLog::emitLog(const Formatter::HttpFormatterContext& context,
                               const StreamInfo::StreamInfo& stream_info) {
  auto msgpack = formatter_->format(context, stream_info);
  uint64_t time = std::chrono::duration_cast<std::chrono::seconds>(
                      stream_info.timeSource().systemTime().time_since_epoch())
                      .count();
  tls_slot_->getTyped<ThreadLocalLogger>().logger_->log(
      std::make_unique<Entry>(time, std::move(msgpack)));
}

} // namespace Fluentd
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
