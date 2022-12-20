#include "prometheus_stats_render.h"
#include "source/server/admin/prometheus_stats_render.h"

#include "envoy/server/admin.h"
#include "envoy/stats/stats.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/server/admin/stats_params.h"
#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

void PrometheusStatsRender::generate(Buffer::Instance& response,
                                     const std::string& prefixed_tag_extracted_name,
                                     const Stats::Gauge& gauge) {
  const std::string tags = PrometheusStatsFormatter::formattedTags(gauge.tags());
  response.add(fmt::format("{0}{{{1}}} {2}\n", prefixed_tag_extracted_name, tags, gauge.value()));
}

// Writes a counter value.
void PrometheusStatsRender::generate(Buffer::Instance& response,
                                     const std::string& prefixed_tag_extracted_name,
                                     const Stats::Counter& counter) {
  const std::string tags = PrometheusStatsFormatter::formattedTags(counter.tags());
  response.add(fmt::format("{0}{{{1}}} {2}\n", prefixed_tag_extracted_name, tags, counter.value()));
}

// Writes a text readout value.
void PrometheusStatsRender::generate(Buffer::Instance& response,
                                     const std::string& prefixed_tag_extracted_name,
                                     const Stats::TextReadout& text_readout) {
  auto tags = text_readout.tags();
  tags.push_back(Stats::Tag{"text_value", text_readout.value()});
  const std::string formattedTags = PrometheusStatsFormatter::formattedTags(tags);
  response.add(fmt::format("{0}{{{1}}} 0\n", prefixed_tag_extracted_name, formattedTags));
}

// Writes a histogram value.
void PrometheusStatsRender::generate(Buffer::Instance& response,
                                     const std::string& prefixed_tag_extracted_name,
                                     const Stats::ParentHistogram& histogram) {
  const std::string tags = PrometheusStatsFormatter::formattedTags(histogram.tags());
  const std::string hist_tags = histogram.tags().empty() ? EMPTY_STRING : (tags + ",");

  const Stats::HistogramStatistics& stats = histogram.cumulativeStatistics();
  Stats::ConstSupportedBuckets& supported_buckets = stats.supportedBuckets();
  const std::vector<uint64_t>& computed_buckets = stats.computedBuckets();
  for (size_t i = 0; i < supported_buckets.size(); ++i) {
    double bucket = supported_buckets[i];
    uint64_t value = computed_buckets[i];
    // We want to print the bucket in a fixed point (non-scientific) format. The fmt library
    // doesn't have a specific modifier to format as a fixed-point value only so we use the
    // 'g' operator which prints the number in general fixed point format or scientific format
    // with precision 50 to round the number up to 32 significant digits in fixed point format
    // which should cover pretty much all cases
    response.add(fmt::format("{0}_bucket{{{1}le=\"{2:.32g}\"}} {3}\n", prefixed_tag_extracted_name,
                             hist_tags, bucket, value));
  }

  response.add(fmt::format("{0}_bucket{{{1}le=\"+Inf\"}} {2}\n", prefixed_tag_extracted_name,
                           hist_tags, stats.sampleCount()));
  response.add(fmt::format("{0}_sum{{{1}}} {2:.32g}\n", prefixed_tag_extracted_name, tags,
                           stats.sampleSum()));
  response.add(fmt::format("{0}_count{{{1}}} {2}\n", prefixed_tag_extracted_name, tags,
                           stats.sampleCount()));
}

} // namespace Server
} // namespace Envoy
