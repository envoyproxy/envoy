#pragma once

#include <chrono>

#include "envoy/extensions/access_loggers/fluentd/v3/fluentd.pb.h"
#include "envoy/extensions/access_loggers/fluentd/v3/fluentd.pb.validate.h"

#include "source/common/formatter/substitution_formatter.h"
#include "source/extensions/access_loggers/common/access_log_base.h"
#include "source/extensions/access_loggers/fluentd/substitution_formatter.h"
#include "source/extensions/common/fluentd/fluentd_base.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Fluentd {

using namespace Envoy::Extensions::Common::Fluentd;
using FluentdAccessLogConfig =
    envoy::extensions::access_loggers::fluentd::v3::FluentdAccessLogConfig;
using FluentdAccessLogConfigSharedPtr = std::shared_ptr<FluentdAccessLogConfig>;

class FluentdAccessLoggerImpl : public FluentdBase {
public:
  FluentdAccessLoggerImpl(Upstream::ThreadLocalCluster& cluster, Tcp::AsyncTcpClientPtr client,
                          Event::Dispatcher& dispatcher, const FluentdAccessLogConfig& config,
                          BackOffStrategyPtr backoff_strategy, Stats::Scope& parent_scope);

  void packMessage(MessagePackPacker& packer);
};

using FluentdAccessLoggerWeakPtr = std::weak_ptr<FluentdService>;
using FluentdAccessLoggerSharedPtr = std::shared_ptr<FluentdService>;

class FluentdAccessLoggerCacheImpl
    : public FluentdCacheBase<FluentdAccessLoggerImpl, FluentdAccessLogConfig,
                              FluentdAccessLoggerSharedPtr, FluentdAccessLoggerWeakPtr>,
      public Singleton::Instance {
public:
  FluentdAccessLoggerCacheImpl(Upstream::ClusterManager& cluster_manager,
                               Stats::Scope& parent_scope, ThreadLocal::SlotAllocator& tls)
      : FluentdCacheBase(cluster_manager, parent_scope, tls, "access_logs.fluentd") {};

protected:
  FluentdAccessLoggerSharedPtr
  createInstance(Upstream::ThreadLocalCluster& cluster, Tcp::AsyncTcpClientPtr client,
                 Event::Dispatcher& dispatcher, const FluentdAccessLogConfig& config,
                 BackOffStrategyPtr backoff_strategy, Random::RandomGenerator&) override {
    return std::make_shared<FluentdAccessLoggerImpl>(cluster, std::move(client), dispatcher, config,
                                                     std::move(backoff_strategy), *stats_scope_);
  }
};

using FluentdAccessLoggerCacheSharedPtr =
    std::shared_ptr<FluentdCache<FluentdAccessLogConfig, FluentdAccessLoggerSharedPtr>>;

/**
 * Access log Instance that writes logs to a Fluentd.
 */
class FluentdAccessLog : public Common::ImplBase {
public:
  FluentdAccessLog(AccessLog::FilterPtr&& filter, FluentdFormatterPtr&& formatter,
                   const FluentdAccessLogConfigSharedPtr config, ThreadLocal::SlotAllocator& tls,
                   Random::RandomGenerator& random,
                   FluentdAccessLoggerCacheSharedPtr access_logger_cache);

private:
  /**
   * Per-thread cached logger.
   */
  struct ThreadLocalLogger : public ThreadLocal::ThreadLocalObject {
    ThreadLocalLogger(FluentdAccessLoggerSharedPtr logger) : logger_(std::move(logger)) {}

    const FluentdAccessLoggerSharedPtr logger_;
  };

  // Common::ImplBase
  void emitLog(const Formatter::HttpFormatterContext& context,
               const StreamInfo::StreamInfo& stream_info) override;

  FluentdFormatterPtr formatter_;
  const ThreadLocal::SlotPtr tls_slot_;
  const FluentdAccessLogConfigSharedPtr config_;
  const FluentdAccessLoggerCacheSharedPtr access_logger_cache_;
};

} // namespace Fluentd
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
