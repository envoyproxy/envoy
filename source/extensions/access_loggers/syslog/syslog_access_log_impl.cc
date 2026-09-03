#include "source/extensions/access_loggers/syslog/syslog_access_log_impl.h"

#include "source/extensions/access_loggers/syslog/formatter.h"
#include "source/extensions/access_loggers/syslog/udp_sender.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

SyslogAccessLoggerImpl::SyslogAccessLoggerImpl(const SyslogAccessLogConfig& config,
                                               Formatter::FormatterConstSharedPtr body_formatter,
                                               SenderPtr sender, SyslogAccessLogStats& stats)
    : sender_(std::move(sender)), stats_(stats),
      max_syslog_message_bytes_(config.max_syslog_message_bytes() == 0
                                    ? DefaultMaxSyslogMessageBytes
                                    : config.max_syslog_message_bytes()) {
  if (config.syslog_format() == SyslogAccessLogConfig::RFC5424) {
    formatter_ = std::make_unique<Rfc5424Formatter>(
        std::move(body_formatter),
        Rfc5424HeaderFormatter(config.facility(), config.severity(), config.omit_hostname(),
                               config.tag(), config.msg_id()));
  } else {
    formatter_ = std::make_unique<Rfc3164Formatter>(
        std::move(body_formatter), Rfc3164HeaderFormatter(config.facility(), config.severity(),
                                                          config.omit_hostname(), config.tag()));
  }
}

void SyslogAccessLoggerImpl::log(const Formatter::Context& context,
                                 const StreamInfo::StreamInfo& stream_info) {
  // format() builds the complete message in a new string. The formatter cannot stop at the Syslog
  // size limit, so truncation happens after formatting is complete.
  std::string message = formatter_->format(context, stream_info);
  const uint64_t original_size = message.size();
  const bool oversized = message.size() > max_syslog_message_bytes_;
  if (oversized) {
    message.resize(max_syslog_message_bytes_);
    stats_.truncated(original_size - message.size());
  } else {
    stats_.full();
  }
  sender_->send(message);
}

SyslogAccessLog::SyslogAccessLog(AccessLog::FilterPtr&& filter, Formatter::FormatterPtr&& formatter,
                                 SyslogAccessLogConfigSharedPtr config,
                                 Network::Address::InstanceConstSharedPtr destination,
                                 ThreadLocal::SlotAllocator& tls, Stats::Scope& scope,
                                 Upstream::ClusterManager& cluster_manager)
    : Common::ImplBase(std::move(filter)), formatter_(std::move(formatter)),
      stats_(scope, config->stat_prefix()), tls_slot_(tls.allocateSlot()),
      config_(std::move(config)), destination_(std::move(destination)) {
  tls_slot_->set([config = config_, formatter = formatter_, destination = destination_,
                  &cluster_manager, stats = &stats_](Event::Dispatcher& dispatcher) {
    SenderPtr sender;
    if (config->has_unix_socket()) {
      sender = std::make_unique<StaticUdpSender>(dispatcher, destination, *stats);
    } else {
      sender = std::make_unique<ClusterUdpSender>(dispatcher, cluster_manager,
                                                  config->cluster_name(), *stats);
    }
    return std::make_shared<ThreadLocalLogger>(
        std::make_shared<SyslogAccessLoggerImpl>(*config, formatter, std::move(sender), *stats));
  });
}

void SyslogAccessLog::emitLog(const Formatter::Context& context,
                              const StreamInfo::StreamInfo& stream_info) {
  tls_slot_->getTyped<ThreadLocalLogger>().logger_->log(context, stream_info);
}

} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
