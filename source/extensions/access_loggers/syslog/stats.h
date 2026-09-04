#pragma once

#include <cstdint>

#include "envoy/stats/scope.h"

#include "source/common/stats/symbol_table.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

class SyslogAccessLogStats {
public:
  SyslogAccessLogStats(Stats::Scope& scope, absl::string_view stat_prefix);

  void full();
  void truncated(uint64_t bytes);
  void sent(uint64_t bytes);

private:
  Stats::ScopeSharedPtr scope_;
  Stats::StatNamePool stat_names_;
  const Stats::StatName bytes_sent_name_;
  const Stats::StatName bytes_truncated_name_;
  const Stats::StatName messages_name_;
  const Stats::StatName send_name_;
  const Stats::StatName state_name_;
  const Stats::StatName full_name_;
  const Stats::StatName truncated_name_;
  const Stats::StatNameTagVector full_tags_;
  const Stats::StatNameTagVector truncated_tags_;
  Stats::Counter& bytes_sent_;
  Stats::Counter& bytes_truncated_;
  Stats::Counter& messages_full_;
  Stats::Counter& messages_truncated_;
  Stats::Counter& send_;
};

} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
