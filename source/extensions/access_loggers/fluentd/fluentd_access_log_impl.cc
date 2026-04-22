#include "source/extensions/access_loggers/fluentd/fluentd_access_log_impl.h"

#include "source/common/buffer/buffer_impl.h"

#include "msgpack.hpp"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Fluentd {

FluentdAccessLoggerImpl::FluentdAccessLoggerImpl(Upstream::ThreadLocalCluster& cluster,
                                                 Tcp::AsyncTcpClientPtr client,
                                                 Event::Dispatcher& dispatcher,
                                                 const FluentdAccessLogConfig& config,
                                                 BackOffStrategyPtr backoff_strategy,
                                                 Stats::Scope& parent_scope)
    : FluentdBase(
          cluster, std::move(client), dispatcher, config.tag(),
          config.has_retry_options() && config.retry_options().has_max_connect_attempts()
              ? absl::optional<uint32_t>(config.retry_options().max_connect_attempts().value())
              : absl::nullopt,
          parent_scope, config.stat_prefix(), std::move(backoff_strategy),
          PROTOBUF_GET_MS_OR_DEFAULT(config, buffer_flush_interval, DefaultBufferFlushIntervalMs),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, buffer_size_bytes, DefaultMaxBufferSize)) {}

void FluentdAccessLoggerImpl::packMessage(MessagePackPacker& packer) {
  // Creating a Fluentd Forward Protocol Specification (v1) forward mode event as specified in:
  // https://github.com/fluent/fluentd/wiki/Forward-Protocol-Specification-v1#forward-mode
  packer.pack_array(2); // 1 - tag field, 2 - entries array.
  packer.pack(tag_);
  packer.pack_array(entries_.size());

  for (auto& entry : entries_) {
    packer.pack_array(2); // 1 - time, 2 - record.
    packer.pack(entry->time_);
    const char* record_bytes = reinterpret_cast<const char*>(&entry->record_[0]);
    packer.pack_bin_body(record_bytes, entry->record_.size());
  }
}

FluentdAccessLog::FluentdAccessLog(AccessLog::FilterPtr&& filter, FluentdFormatterPtr&& formatter,
                                   const FluentdAccessLogConfigSharedPtr config,
                                   ThreadLocal::SlotAllocator& tls, Random::RandomGenerator& random,
                                   FluentdAccessLoggerCacheSharedPtr access_logger_cache)
    : ImplBase(std::move(filter)), formatter_(std::move(formatter)), tls_slot_(tls.allocateSlot()),
      config_(config), access_logger_cache_(access_logger_cache) {

  uint64_t base_interval_ms = DefaultBaseBackoffIntervalMs;
  uint64_t max_interval_ms = base_interval_ms * DefaultMaxBackoffIntervalFactor;

  if (config->has_retry_options() && config->retry_options().has_backoff_options()) {
    base_interval_ms = PROTOBUF_GET_MS_OR_DEFAULT(config->retry_options().backoff_options(),
                                                  base_interval, DefaultBaseBackoffIntervalMs);
    max_interval_ms =
        PROTOBUF_GET_MS_OR_DEFAULT(config->retry_options().backoff_options(), max_interval,
                                   base_interval_ms * DefaultMaxBackoffIntervalFactor);
  }

  tls_slot_->set([config = config_, &random, access_logger_cache = access_logger_cache_,
                  base_interval_ms, max_interval_ms](Event::Dispatcher&) {
    BackOffStrategyPtr backoff_strategy = std::make_unique<JitteredExponentialBackOffStrategy>(
        base_interval_ms, max_interval_ms, random);
    return std::make_shared<ThreadLocalLogger>(
        access_logger_cache->getOrCreate(config, random, std::move(backoff_strategy)));
  });
}

void FluentdAccessLog::emitLog(const Formatter::Context& context,
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
