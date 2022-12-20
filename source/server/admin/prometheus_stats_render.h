#pragma once

#include "envoy/server/admin.h"
#include "envoy/stats/stats.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/server/admin/stats_params.h"
#include "source/server/admin/utils.h"

#include "prometheus_stats.h"

namespace Envoy {
namespace Server {

class PrometheusStatsRender {
public:
  // Writes a gauge value.
  void generate(Buffer::Instance& response, const std::string& prefixed_tag_extracted_name,
                const Stats::Gauge& gauge);

  // Writes a counter value.
  void generate(Buffer::Instance& response, const std::string& prefixed_tag_extracted_name,
                const Stats::Counter& counter);

  // Writes a text readout value.
  void generate(Buffer::Instance& response, const std::string& prefixed_tag_extracted_name,
                const Stats::TextReadout& text_readout);

  // Writes a histogram value.
  void generate(Buffer::Instance& response, const std::string& prefixed_tag_extracted_name,
                const Stats::ParentHistogram& histogram);

  // Completes rendering any buffered data.
  void finalize(Buffer::Instance&) {}

  // Indicates that no stats for a particular type have been found.
  void noStats(Buffer::Instance&, absl::string_view) {}
};

} // namespace Server
} // namespace Envoy
