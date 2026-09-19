#include "source/extensions/access_loggers/syslog/stats.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

namespace {

constexpr absl::string_view SyslogStatsPrefix = "access_logs.syslog.";

} // namespace

SyslogAccessLogStats::SyslogAccessLogStats(Stats::Scope& scope, absl::string_view stat_prefix)
    : scope_(scope.createScope(absl::StrCat(SyslogStatsPrefix, stat_prefix, "."))),
      stat_names_(scope_->symbolTable()), bytes_sent_name_(stat_names_.add("bytes_sent")),
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

void SyslogAccessLogStats::full() { messages_full_.inc(); }

void SyslogAccessLogStats::truncated(uint64_t bytes) {
  messages_truncated_.inc();
  bytes_truncated_.add(bytes);
}

void SyslogAccessLogStats::sent(uint64_t bytes) {
  send_.inc();
  bytes_sent_.add(bytes);
}

} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
