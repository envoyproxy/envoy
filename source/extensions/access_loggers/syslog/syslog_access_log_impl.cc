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
constexpr uint32_t ProtoEnumCodeOffset = 1;
constexpr uint32_t Local7FacilityCode = 23;
constexpr uint32_t InfoSeverityCode = 6;
constexpr uint32_t DebugSeverityCode = 7;
constexpr uint32_t SeveritiesPerFacility = 8;

uint32_t facilityCode(const SyslogAccessLogConfig::Facility facility) {
  return facility == SyslogAccessLogConfig::FACILITY_LOCAL7
             ? Local7FacilityCode
             : static_cast<uint32_t>(facility) - ProtoEnumCodeOffset;
}

uint32_t severityCode(const SyslogAccessLogConfig::Severity severity) {
  switch (severity) {
  case SyslogAccessLogConfig::INFO:
    return InfoSeverityCode;
  case SyslogAccessLogConfig::DEBUG:
    return DebugSeverityCode;
  default:
    return static_cast<uint32_t>(severity) - ProtoEnumCodeOffset;
  }
}

Formatter::FormatterPtr makeFormatter(const SyslogAccessLogConfig& config,
                                      Formatter::FormatterConstSharedPtr body_formatter) {
  const std::string priority = absl::StrCat(
      "<",
      facilityCode(config.facility()) * SeveritiesPerFacility + severityCode(config.severity()),
      ">");
  const std::string hostname =
      config.no_hostname() ? "" : Formatter::SubstitutionFormatUtils::getHostname().value_or("");
  const std::string app_name = config.tag().empty() ? std::string(DefaultAppName) : config.tag();

  switch (config.syslog_format()) {
  case SyslogAccessLogConfig::RFC3164:
    return std::make_unique<Rfc3164Formatter>(std::move(body_formatter), priority, hostname,
                                              app_name);
  case SyslogAccessLogConfig::RFC5424: {
    const std::string msg_id =
        config.msg_id().empty() ? std::string(DefaultRfc5424MessageId) : config.msg_id();
    return std::make_unique<Rfc5424Formatter>(std::move(body_formatter), priority, hostname,
                                              app_name, msg_id);
  }
  default:
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
}

SenderPtr makeSender(const SyslogAccessLogConfig& config,
                     Network::Address::InstanceConstSharedPtr destination,
                     Event::Dispatcher& dispatcher, Upstream::ClusterManager& cluster_manager,
                     SyslogAccessLogStats& stats) {
  switch (config.destination_case()) {
  case SyslogAccessLogConfig::kServer:
    switch (config.server().address_case()) {
    case envoy::config::core::v3::Address::kPipe:
      return std::make_unique<StaticUdpSender>(dispatcher, std::move(destination), stats);
    case envoy::config::core::v3::Address::kSocketAddress:
      switch (config.server().socket_address().protocol()) {
      case envoy::config::core::v3::SocketAddress::UDP:
        return std::make_unique<StaticUdpSender>(dispatcher, std::move(destination), stats);
      case envoy::config::core::v3::SocketAddress::TCP:
        // TODO(izumi39): Add RFC 6587 TCP support for server destinations.
        throw EnvoyException("syslog over TCP is not implemented yet");
      default:
        throw EnvoyException("invalid syslog server protocol");
      }
    default:
      throw EnvoyException("invalid syslog server address type");
    }
  case SyslogAccessLogConfig::kCluster:
    switch (config.cluster().protocol()) {
    case SyslogAccessLogConfig::Cluster::UDP:
      return std::make_unique<ClusterUdpSender>(dispatcher, cluster_manager,
                                                config.cluster().name(), stats);
    case SyslogAccessLogConfig::Cluster::TCP:
      // TODO(izumi39): Add RFC 6587 TCP support for cluster destinations.
      throw EnvoyException("syslog over TCP is not implemented yet");
    default:
      throw EnvoyException("invalid syslog cluster protocol");
    }
  default:
    throw EnvoyException("syslog destination is not configured");
  }
}

std::string statsPrefix(absl::string_view stat_prefix) {
  return stat_prefix.empty() ? std::string(SyslogStatsPrefix)
                             : absl::StrCat(SyslogStatsPrefix, stat_prefix, ".");
}

} // namespace

SyslogAccessLogStats::SyslogAccessLogStats(Stats::Scope& scope, absl::string_view stat_prefix)
    : scope_(scope.createScope(statsPrefix(stat_prefix))), stat_names_(scope_->symbolTable()),
      bytes_sent_name_(stat_names_.add("bytes_sent")), dropped_name_(stat_names_.add("dropped")),
      send_name_(stat_names_.add("send")), state_name_(stat_names_.add("state")),
      full_name_(stat_names_.add("full")), truncated_name_(stat_names_.add("truncated")),
      full_tags_({{state_name_, full_name_}}), truncated_tags_({{state_name_, truncated_name_}}),
      bytes_sent_(scope_->counterFromStatName(bytes_sent_name_)),
      dropped_(scope_->counterFromStatName(dropped_name_)),
      send_full_(scope_->counterFromTaggedName(send_name_, Stats::StatNameTagSpan(full_tags_), {})),
      send_truncated_(
          scope_->counterFromTaggedName(send_name_, Stats::StatNameTagSpan(truncated_tags_), {})) {}

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
  // size limit, so we can only truncate or drop the message after formatting is complete.
  std::string message = formatter_->format(context, stream_info);
  const bool oversized = message.size() > max_message_size_;
  if (oversized) {
    if (skip_oversized_message_) {
      ENVOY_LOG_MISC(debug, "syslog access log message is too large ({} bytes), skipping",
                     message.size());
      stats_.dropped_.inc();
      return;
    }

    message.resize(max_message_size_);
  }
  sender_->send(message);
  (oversized ? stats_.send_truncated_ : stats_.send_full_).inc();
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
