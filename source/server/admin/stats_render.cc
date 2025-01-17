#include "source/server/admin/stats_render.h"

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
  case Utility::HistogramBucketsMode::Unset:
  case Utility::HistogramBucketsMode::Summary:
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
    response.addFragments({delim, absl::StrFormat("%.15g,%.15g:%lu", bucket.lower_bound_,
                                                  bucket.width_, bucket.count_)});
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
    : histogram_buckets_mode_(params.histogram_buckets_mode_),
      json_(std::make_unique<JsonContext>(response)), response_(response) {
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
}

StatsJsonRender::JsonContext::JsonContext(Buffer::Instance& response)
    : streamer_(response), stats_map_(streamer_.makeRootMap()) {
  // We don't create a JSON data model for the stats output, as that makes
  // streaming difficult. Instead we emit the preamble in the constructor here,
  // and create json models for each stats entry.
  stats_map_->addKey("stats");
  stats_array_ = stats_map_->addArray();
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
  json_->stats_array_->addMap()->addEntries({{"name", name}, {"value", value}});
  drainIfNeeded(response);
}

// Buffers a JSON fragment for a text-readout stat, flushing to the response
// buffer once we exceed JsonStatsFlushCount stats.
void StatsJsonRender::generate(Buffer::Instance& response, const std::string& name,
                               const std::string& value) {
  ASSERT(!histograms_initialized_);
  json_->stats_array_->addMap()->addEntries({{"name", name}, {"value", value}});
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
  case Utility::HistogramBucketsMode::Unset:
  case Utility::HistogramBucketsMode::Summary: {
    Json::BufferStreamer::MapPtr map = json_->histogram_array_->addMap();
    map->addEntries({{"name", name}});
    map->addKey("values");
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
  case Utility::HistogramBucketsMode::Detailed: {
    generateHistogramDetail(name, histogram, *json_->histogram_array_->addMap());
    break;
  }
  }
  drainIfNeeded(response);
}

void StatsJsonRender::populateSupportedPercentiles(Json::BufferStreamer::Array& array) {
  Stats::HistogramStatisticsImpl empty_statistics;
  std::vector<double> supported = empty_statistics.supportedQuantiles();
  std::vector<Json::BufferStreamer::Value> views(supported.size());
  for (uint32_t i = 0, n = supported.size(); i < n; ++i) {
    views[i] = supported[i] * 100;
  }
  array.addEntries(views);
}

void StatsJsonRender::populatePercentiles(const Stats::ParentHistogram& histogram,
                                          Json::BufferStreamer::Map& map) {
  Json::BufferStreamer::ArrayPtr array = map.addArray();
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
  json_->histogram_map1_ = json_->stats_array_->addMap();
  json_->histogram_map1_->addKey("histograms");
  switch (histogram_buckets_mode_) {
  case Utility::HistogramBucketsMode::Detailed:
    json_->histogram_map2_ = json_->histogram_map1_->addMap();
    json_->histogram_map2_->addKey("supported_percentiles");
    { populateSupportedPercentiles(*json_->histogram_map2_->addArray()); }
    json_->histogram_map2_->addKey("details");
    json_->histogram_array_ = json_->histogram_map2_->addArray();
    break;
  case Utility::HistogramBucketsMode::Unset:
  case Utility::HistogramBucketsMode::Summary:
    json_->histogram_map2_ = json_->histogram_map1_->addMap();
    json_->histogram_map2_->addKey("supported_quantiles");
    { populateSupportedPercentiles(*json_->histogram_map2_->addArray()); }
    json_->histogram_map2_->addKey("computed_quantiles");
    json_->histogram_array_ = json_->histogram_map2_->addArray();
    break;
  case Utility::HistogramBucketsMode::Cumulative:
  case Utility::HistogramBucketsMode::Disjoint:
    json_->histogram_array_ = json_->histogram_map1_->addArray();
    break;
  }
}

void StatsJsonRender::generateHistogramDetail(const std::string& name,
                                              const Stats::ParentHistogram& histogram,
                                              Json::BufferStreamer::Map& map) {
  // Now we produce the stream-able histogram records, without using the json intermediate
  // representation or serializer.
  map.addEntries({{"name", name}});
  map.addKey("totals");
  populateBucketsVerbose(histogram.detailedTotalBuckets(), map);
  map.addKey("intervals");
  populateBucketsVerbose(histogram.detailedIntervalBuckets(), map);
  map.addKey("percentiles");
  populatePercentiles(histogram, map);
}

void StatsJsonRender::populateBucketsVerbose(
    const std::vector<Stats::ParentHistogram::Bucket>& buckets, Json::BufferStreamer::Map& map) {
  Json::BufferStreamer::ArrayPtr buckets_array = map.addArray();
  for (const Stats::ParentHistogram::Bucket& bucket : buckets) {
    buckets_array->addMap()->addEntries(
        {{"lower_bound", bucket.lower_bound_}, {"width", bucket.width_}, {"count", bucket.count_}});
  }
}

// Since histograms are buffered (see above), the finalize() method generates
// all of them.
void StatsJsonRender::finalize(Buffer::Instance& response) {
  json_.reset();
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

  Json::BufferStreamer::MapPtr map = json_->histogram_array_->addMap();
  map->addEntries({{"name", name}});
  map->addKey("buckets");
  Json::BufferStreamer::ArrayPtr buckets = map->addArray();
  for (uint32_t i = 0; i < min_size; ++i) {
    Json::BufferStreamer::MapPtr bucket_map = buckets->addMap();
    bucket_map->addEntries({{"upper_bound", supported_buckets[i]},
                            {"interval", interval_buckets[i]},
                            {"cumulative", cumulative_buckets[i]}});
  }
}

} // namespace Server
} // namespace Envoy
