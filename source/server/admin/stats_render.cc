#include "source/server/admin/stats_render.h"

#include "source/common/json/json_sanitizer.h"
#include "source/common/stats/histogram_impl.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Server {

StatsTextRender::StatsTextRender(const StatsParams& params)
    : histogram_buckets_mode_(params.histogram_buckets_mode_) {}

void StatsTextRender::generate(Buffer::Instance& response, const std::string& name,
                               uint64_t value) {
  response.addFragments({name, ": ", absl::StrCat(value), "\n"});
}

void StatsTextRender::generate(Buffer::Instance& response, const std::string& name,
                               const std::string& value) {
  response.addFragments({name, ": \"", value, "\"\n"});
}

void StatsTextRender::generate(Buffer::Instance& response, const std::string& name,
                               const Stats::ParentHistogram& histogram) {
  if (!histogram.used()) {
    response.addFragments({name, ": No recorded values\n"});
    return;
  }

  switch (histogram_buckets_mode_) {
  case Utility::HistogramBucketsMode::NoBuckets:
    response.addFragments({name, ": ", histogram.quantileSummary(), "\n"});
    break;
  case Utility::HistogramBucketsMode::Cumulative:
    response.addFragments({name, ": ", histogram.bucketSummary(), "\n"});
    break;
  case Utility::HistogramBucketsMode::Disjoint:
    addDisjointBuckets(name, histogram, response);
    break;
  case Utility::HistogramBucketsMode::Detailed:
    response.addFragments({name, ":\n  totals="});
    addDetail(histogram.detailedTotalBuckets(), response);
    response.add("\n  intervals=");
    addDetail(histogram.detailedIntervalBuckets(), response);
    response.addFragments({"\n  summary=", histogram.quantileSummary(), "\n"});
    break;
  }
}

void StatsTextRender::finalize(Buffer::Instance&) {}

void StatsTextRender::addDetail(const std::vector<Stats::ParentHistogram::Bucket>& buckets,
                                Buffer::Instance& response) {
  absl::string_view delim = "";
  for (const Stats::ParentHistogram::Bucket& bucket : buckets) {
    response.addFragments(
        {delim, absl::StrCat(bucket.lower_bound_, ",", bucket.width_, ":", bucket.count_)});
    delim = ", ";
  }
}

// Computes disjoint buckets as text and adds them to the response buffer.
void StatsTextRender::addDisjointBuckets(const std::string& name,
                                         const Stats::ParentHistogram& histogram,
                                         Buffer::Instance& response) {
  if (!histogram.used()) {
    response.addFragments({name, ": No recorded values\n"});
    return;
  }
  response.addFragments({name, ": "});
  std::vector<absl::string_view> bucket_summary;

  const Stats::HistogramStatistics& interval_statistics = histogram.intervalStatistics();
  Stats::ConstSupportedBuckets& supported_buckets = interval_statistics.supportedBuckets();
  const std::vector<uint64_t> disjoint_interval_buckets =
      interval_statistics.computeDisjointBuckets();
  const std::vector<uint64_t> disjoint_cumulative_buckets =
      histogram.cumulativeStatistics().computeDisjointBuckets();
  // Make sure all vectors are the same size.
  ASSERT(disjoint_interval_buckets.size() == disjoint_cumulative_buckets.size());
  ASSERT(disjoint_cumulative_buckets.size() == supported_buckets.size());
  const size_t min_size = std::min({disjoint_interval_buckets.size(),
                                    disjoint_cumulative_buckets.size(), supported_buckets.size()});
  std::vector<std::string> bucket_strings;
  bucket_strings.reserve(min_size);
  for (size_t i = 0; i < min_size; ++i) {
    if (i != 0) {
      bucket_summary.push_back(" ");
    }
    bucket_strings.push_back(fmt::format("B{:g}({},{})", supported_buckets[i],
                                         disjoint_interval_buckets[i],
                                         disjoint_cumulative_buckets[i]));
    bucket_summary.push_back(bucket_strings.back());
  }
  bucket_summary.push_back("\n");
  response.addFragments(bucket_summary);
}

StatsJsonRender::StatsJsonRender(Http::ResponseHeaderMap& response_headers,
                                 Buffer::Instance& response, const StatsParams& params)
    : histogram_buckets_mode_(params.histogram_buckets_mode_), json_streamer_(response),
      json_stats_map_(json_streamer_.makeRootMap()), response_(response) {
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  // We don't create a JSON data model for the stats output, as that makes
  // streaming difficult. Instead we emit the preamble in the constructor here,
  // and create json models for each stats entry.
  json_stats_map_->newKey("stats");
  json_stats_array_ = json_stats_map_->addArray();
}

void StatsJsonRender::drainIfNeeded(Buffer::Instance& response) {
  if (&response_ != &response) {
    response.move(response_);
  }
}

// Buffers a JSON fragment for a numeric stats, flushing to the response
// buffer once we exceed JsonStatsFlushCount stats.
void StatsJsonRender::generate(Buffer::Instance& response, const std::string& name,
                               uint64_t value) {
  ASSERT(!histograms_initialized_);
  json_stats_array_->addMap()->addEntries({{"name", name}, {"value", static_cast<double>(value)}});
  drainIfNeeded(response);
}

// Buffers a JSON fragment for a text-readout stat, flushing to the response
// buffer once we exceed JsonStatsFlushCount stats.
void StatsJsonRender::generate(Buffer::Instance& response, const std::string& name,
                               const std::string& value) {
  ASSERT(!histograms_initialized_);
  json_stats_array_->addMap()->addEntries({{"name", name}, {"value", value}});
  drainIfNeeded(response);
}

// In JSON we buffer all histograms and don't write them immediately, so we
// can, in one JSON structure, emit shared attributes of all histograms and
// each individual histogram.
//
// This is counter to the goals of streaming and chunked interfaces, but
// usually there are far fewer histograms than counters or gauges.
//
// We can further optimize this by streaming out the histograms object, one
// histogram at a time, in case buffering all the histograms in Envoy
// buffers up too much memory.
void StatsJsonRender::generate(Buffer::Instance& response, const std::string& name,
                               const Stats::ParentHistogram& histogram) {
  if (!histograms_initialized_) {
    renderHistogramStart();
  }

  switch (histogram_buckets_mode_) {
  case Utility::HistogramBucketsMode::NoBuckets: {
    Json::Streamer::MapPtr map = json_histogram_array_->addMap();
    map->addEntries({{"name", name}});
    map->newKey("values");
    populatePercentiles(histogram, *map);
    break;
  }
  case Utility::HistogramBucketsMode::Cumulative: {
    const Stats::HistogramStatistics& interval_statistics = histogram.intervalStatistics();
    const std::vector<uint64_t>& interval_buckets = interval_statistics.computedBuckets();
    const std::vector<uint64_t>& cumulative_buckets =
        histogram.cumulativeStatistics().computedBuckets();
    collectBuckets(name, histogram, interval_buckets, cumulative_buckets);
    break;
  }
  case Utility::HistogramBucketsMode::Disjoint: {
    const Stats::HistogramStatistics& interval_statistics = histogram.intervalStatistics();
    const std::vector<uint64_t> interval_buckets = interval_statistics.computeDisjointBuckets();
    const std::vector<uint64_t> cumulative_buckets =
        histogram.cumulativeStatistics().computeDisjointBuckets();
    collectBuckets(name, histogram, interval_buckets, cumulative_buckets);
    break;
  }
  case Utility::HistogramBucketsMode::Detailed:
    generateHistogramDetail(name, histogram);
    break;
  }
  drainIfNeeded(response);
}

void StatsJsonRender::populateSupportedPercentiles(Json::Streamer::Map& map) {
  Stats::HistogramStatisticsImpl empty_statistics;
  std::vector<double> supported = empty_statistics.supportedQuantiles();
  std::vector<std::string> supported_strings(supported.size());
  std::vector<Json::Streamer::Value> views(supported.size());
  uint32_t i = 0;
  for (double quantile : supported) {
    views[i] = quantile * 100;
    ++i;
  }
  map.addArray()->addEntries(views);
}

void StatsJsonRender::populatePercentiles(const Stats::ParentHistogram& histogram,
                                          Json::Streamer::Map& map) {
  Json::Streamer::ArrayPtr array = map.addArray();
  std::vector<double> totals = histogram.cumulativeStatistics().computedQuantiles(),
                      intervals = histogram.intervalStatistics().computedQuantiles();
  uint32_t min_size = std::min(totals.size(), intervals.size());
  ASSERT(totals.size() == min_size);
  ASSERT(intervals.size() == min_size);
  for (uint32_t i = 0; i < min_size; ++i) {
    array->addMap()->addEntries({{"cumulative", totals[i]}, {"interval", intervals[i]}});
  }
};

void StatsJsonRender::renderHistogramStart() {
  histograms_initialized_ = true;
  json_histogram_map1_ = json_stats_array_->addMap();
  json_histogram_map1_->newKey("histograms");
  switch (histogram_buckets_mode_) {
  case Utility::HistogramBucketsMode::Detailed:
    json_histogram_map2_ = json_histogram_map1_->addMap();
    json_histogram_map2_->newKey("supported_percentiles");
    populateSupportedPercentiles(*json_histogram_map2_);
    json_histogram_map2_->newKey("details");
    json_histogram_array_ = json_histogram_map2_->addArray();
    break;
  case Utility::HistogramBucketsMode::NoBuckets:
    json_histogram_map2_ = json_histogram_map1_->addMap();
    json_histogram_map2_->newKey("supported_quantiles");
    populateSupportedPercentiles(*json_histogram_map2_);
    json_histogram_map2_->newKey("computed_quantiles");
    json_histogram_array_ = json_histogram_map2_->addArray();
    break;
  case Utility::HistogramBucketsMode::Cumulative:
  case Utility::HistogramBucketsMode::Disjoint:
    json_histogram_array_ = json_histogram_map1_->addArray();
    break;
  }
}

void StatsJsonRender::generateHistogramDetail(const std::string& name,
                                              const Stats::ParentHistogram& histogram) {
  // Now we produce the stream-able histogram records, without using the json intermediate
  // representation or serializer.
  Json::Streamer::MapPtr map = json_histogram_array_->addMap();
  map->addEntries({{"name", name}});
  map->newKey("totals");
  populateBucketsVerbose(histogram.detailedTotalBuckets(), *map);
  map->newKey("intervals");
  populateBucketsVerbose(histogram.detailedIntervalBuckets(), *map);
  map->newKey("percentiles");
  populatePercentiles(histogram, *map);
}

void StatsJsonRender::populateBucketsVerbose(
    const std::vector<Stats::ParentHistogram::Bucket>& buckets, Json::Streamer::Map& map) {
  Json::Streamer::ArrayPtr buckets_array = map.addArray();
  for (const Stats::ParentHistogram::Bucket& bucket : buckets) {
    buckets_array->addMap()->addEntries({{"lower_bound", bucket.lower_bound_},
                                         {"width", bucket.width_},
                                         {"count", static_cast<double>(bucket.count_)}});
  }
}

// Since histograms are buffered (see above), the finalize() method generates
// all of them.
void StatsJsonRender::finalize(Buffer::Instance& response) {
  json_streamer_.clear();
  drainIfNeeded(response);
}

// Collects the buckets from the specified histogram, using either the
// cumulative or disjoint views, as controlled by buckets_fn.
void StatsJsonRender::collectBuckets(const std::string& name,
                                     const Stats::ParentHistogram& histogram,
                                     const std::vector<uint64_t>& interval_buckets,
                                     const std::vector<uint64_t>& cumulative_buckets) {
  const Stats::HistogramStatistics& interval_statistics = histogram.intervalStatistics();
  Stats::ConstSupportedBuckets& supported_buckets = interval_statistics.supportedBuckets();

  // Make sure all vectors are the same size.
  ASSERT(interval_buckets.size() == cumulative_buckets.size());
  ASSERT(cumulative_buckets.size() == supported_buckets.size());
  size_t min_size =
      std::min({interval_buckets.size(), cumulative_buckets.size(), supported_buckets.size()});

  Json::Streamer::MapPtr map = json_histogram_array_->addMap();
  map->addEntries({{"name", name}});
  map->newKey("buckets");
  Json::Streamer::ArrayPtr buckets = map->addArray();
  for (uint32_t i = 0; i < min_size; ++i) {
    Json::Streamer::MapPtr bucket_map = buckets->addMap();
    bucket_map->addEntries({{"upper_bound", supported_buckets[i]},
                            {"interval", static_cast<double>(interval_buckets[i])},
                            {"cumulative", static_cast<double>(cumulative_buckets[i])}});
  }
}

} // namespace Server
} // namespace Envoy
