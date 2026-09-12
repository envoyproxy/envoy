#pragma once

#include <memory>
#include <string>
#include <utility>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/access_loggers/syslog/v3/syslog.pb.h"
#include "envoy/extensions/access_loggers/syslog/v3/syslog.pb.validate.h"
#include "envoy/network/address.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/extensions/access_loggers/common/access_log_base.h"
#include "source/extensions/access_loggers/syslog/sender.h"
#include "source/extensions/access_loggers/syslog/stats.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

using SyslogAccessLogConfig = envoy::extensions::access_loggers::syslog::v3::SyslogAccessLogConfig;
using SyslogAccessLogConfigSharedPtr = std::shared_ptr<SyslogAccessLogConfig>;

/** Worker-local logger that formats and sends syslog records. */
class SyslogAccessLoggerImpl : public Logger::Loggable<Logger::Id::misc> {
public:
  SyslogAccessLoggerImpl(const SyslogAccessLogConfig& config,
                         Formatter::FormatterConstSharedPtr body_formatter, SenderPtr sender,
                         SyslogAccessLogStats& stats);

  void log(const Formatter::Context& context, const StreamInfo::StreamInfo& stream_info);

private:
  // Avoid IP fragmentation for IPv4 and IPv6 when the path MTU is 1500 bytes.
  static constexpr uint64_t DefaultMaxSyslogMessageBytes = 1452;

  SenderPtr sender_;
  SyslogAccessLogStats& stats_;
  Formatter::FormatterPtr formatter_;
  const uint64_t max_syslog_message_bytes_ = DefaultMaxSyslogMessageBytes;
};

using SyslogAccessLoggerSharedPtr = std::shared_ptr<SyslogAccessLoggerImpl>;

class SyslogAccessLog : public Common::ImplBase {
public:
  SyslogAccessLog(AccessLog::FilterPtr&& filter, Formatter::FormatterPtr&& formatter,
                  SyslogAccessLogConfigSharedPtr config,
                  Network::Address::InstanceConstSharedPtr destination,
                  ThreadLocal::SlotAllocator& tls, Stats::Scope& scope,
                  Upstream::ClusterManager& cluster_manager);

private:
  struct ThreadLocalLogger : public ThreadLocal::ThreadLocalObject {
    explicit ThreadLocalLogger(SyslogAccessLoggerSharedPtr logger) : logger_(std::move(logger)) {}

    const SyslogAccessLoggerSharedPtr logger_;
  };

  // Common::ImplBase
  void emitLog(const Formatter::Context& context,
               const StreamInfo::StreamInfo& stream_info) override;

  const Formatter::FormatterConstSharedPtr formatter_;
  SyslogAccessLogStats stats_;
  const ThreadLocal::SlotSharedPtr tls_slot_;
  const SyslogAccessLogConfigSharedPtr config_;
  const Network::Address::InstanceConstSharedPtr destination_;
};

} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
