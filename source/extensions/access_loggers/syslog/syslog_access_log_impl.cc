#include "source/extensions/access_loggers/syslog/syslog_access_log_impl.h"

#include "source/common/common/utility.h"
#include "source/common/formatter/substitution_format_utility.h"
#include "source/extensions/access_loggers/syslog/rfc3164_formatter.h"
#include "source/extensions/access_loggers/syslog/rfc5424_formatter.h"
#include "source/extensions/access_loggers/syslog/udp_sender.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

namespace {

constexpr absl::string_view DefaultAppName = "envoy";
constexpr absl::string_view SyslogStatsPrefix = "access_logs.syslog.";

// Proto 0 is the Envoy default (LOCAL7 / INFO). Remaining enumerators are RFC
// 5424 codes + 1 so they stay non-zero. DEBUG is already RFC 7, so it is not
// shifted.
uint32_t rfcPriority(SyslogAccessLogConfig::Facility facility_enum,
                     SyslogAccessLogConfig::Severity severity_enum) {
  uint32_t facility = static_cast<uint32_t>(facility_enum);
  if (facility_enum == SyslogAccessLogConfig::FACILITY_LOCAL7) {
    facility = 23;
  } else {
    --facility;
  }

  uint32_t severity = static_cast<uint32_t>(severity_enum);
  if (severity_enum == SyslogAccessLogConfig::INFO) {
    severity = 6;
  } else if (severity_enum != SyslogAccessLogConfig::DEBUG) {
    --severity;
  }
  return facility * 8 + severity;
}

Formatter::FormatterPtr makeFormatter(const SyslogAccessLogConfig& config,
                                      Formatter::FormatterConstSharedPtr body_formatter) {
  const std::string priority =
      absl::StrCat("<", rfcPriority(config.facility(), config.severity()), ">");
  const std::string hostname =
      config.no_hostname() ? "" : Formatter::SubstitutionFormatUtils::getHostname().value_or("");
  const std::string app_name = config.tag().empty() ? std::string(DefaultAppName) : config.tag();

  if (config.syslog_format() == SyslogAccessLogConfig::RFC5424) {
    const std::string msg_id =
        config.msg_id().empty() ? std::string(DefaultRfc5424MessageId) : config.msg_id();
    return std::make_unique<Rfc5424Formatter>(std::move(body_formatter), priority, hostname,
                                              app_name, msg_id);
  }
  return std::make_unique<Rfc3164Formatter>(std::move(body_formatter), priority, hostname,
                                            app_name);
}

SenderPtr makeSender(const SyslogAccessLogConfig& config,
                     Network::Address::InstanceConstSharedPtr destination,
                     Event::Dispatcher& dispatcher, Upstream::ClusterManager& cluster_manager,
                     SyslogAccessLogStats& stats) {
  if (config.has_server()) {
    return std::make_unique<StaticUdpSender>(dispatcher, std::move(destination), stats);
  }
  return std::make_unique<ClusterUdpSender>(dispatcher, cluster_manager, config.cluster().name(),
                                            stats);
}

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
      formatter_(makeFormatter(config, std::move(body_formatter))),
      max_message_size_(config.max_syslog_msg_bytes() == 0 ? DefaultMaxMessageSize
                                                           : config.max_syslog_msg_bytes()) {}

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
    SenderPtr sender = makeSender(*config, destination, dispatcher, cluster_manager, *stats);
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
