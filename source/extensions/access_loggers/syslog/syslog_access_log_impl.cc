#include "source/extensions/access_loggers/syslog/syslog_access_log_impl.h"

#include "source/extensions/access_loggers/syslog/formatter.h"
#include "source/extensions/access_loggers/syslog/udp_sender.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

namespace {

constexpr absl::string_view SyslogStatsPrefix = "access_logs.syslog.";

std::string statsPrefix(absl::string_view stat_prefix) {
  return absl::StrCat(SyslogStatsPrefix, stat_prefix, ".");
}

} // namespace

SyslogAccessLogStats::SyslogAccessLogStats(Stats::Scope& scope, absl::string_view stat_prefix)
    : scope_(scope.createScope(statsPrefix(stat_prefix))), stat_names_(scope_->symbolTable()),
      bytes_sent_name_(stat_names_.add("bytes_sent")),
      bytes_truncated_name_(stat_names_.add("bytes_truncated")),
      messages_name_(stat_names_.add("messages")), send_name_(stat_names_.add("send")),
      state_name_(stat_names_.add("state")), full_name_(stat_names_.add("full")),
      truncated_name_(stat_names_.add("truncated")), full_tags_({{state_name_, full_name_}}),
      truncated_tags_({{state_name_, truncated_name_}}),
      bytes_sent_(scope_->counterFromStatName(bytes_sent_name_)),
      bytes_truncated_(scope_->counterFromStatName(bytes_truncated_name_)),
      messages_full_(
          scope_->counterFromTaggedName(messages_name_, Stats::StatNameTagSpan(full_tags_), {})),
      messages_truncated_(scope_->counterFromTaggedName(
          messages_name_, Stats::StatNameTagSpan(truncated_tags_), {})),
      send_(scope_->counterFromStatName(send_name_)) {}

SyslogAccessLoggerImpl::SyslogAccessLoggerImpl(const SyslogAccessLogConfig& config,
                                               Formatter::FormatterConstSharedPtr body_formatter,
                                               SenderPtr sender, SyslogAccessLogStats& stats)
    : sender_(std::move(sender)), stats_(stats),
      max_message_size_(config.max_syslog_msg_bytes() == 0 ? DefaultMaxMessageSize
                                                           : config.max_syslog_msg_bytes()) {
  if (config.syslog_format() == SyslogAccessLogConfig::RFC5424) {
    formatter_ = std::make_unique<Rfc5424Formatter>(
        std::move(body_formatter),
        Rfc5424HeaderFormatter(config.facility(), config.severity(), config.no_hostname(),
                               config.tag(), config.msg_id()));
  } else {
    formatter_ = std::make_unique<Rfc3164Formatter>(
        std::move(body_formatter),
        Rfc3164HeaderFormatter(config.facility(), config.severity(), config.no_hostname(),
                               config.tag()));
  }
}

void SyslogAccessLoggerImpl::log(const Formatter::Context& context,
                                 const StreamInfo::StreamInfo& stream_info) {
  // format() builds the complete message in a new string. The formatter cannot stop at the Syslog
  // size limit, so truncation happens after formatting is complete.
  std::string message = formatter_->format(context, stream_info);
  const uint64_t original_size = message.size();
  const bool oversized = message.size() > max_message_size_;
  if (oversized) {
    message.resize(max_message_size_);
    stats_.bytes_truncated_.add(original_size - message.size());
  }
  (oversized ? stats_.messages_truncated_ : stats_.messages_full_).inc();
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
    if (config->has_server()) {
      sender = std::make_unique<StaticUdpSender>(dispatcher, destination, *stats);
    } else {
      sender = std::make_unique<ClusterUdpSender>(dispatcher, cluster_manager,
                                                  config->cluster().name(), *stats);
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
