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
      json_stats_map_(json_streamer_.newMap()), response_(response) {
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  // We don't create a JSON data model for the stats output, as that makes
  // streaming difficult. Instead we emit the preamble in the constructor here,
  // and create json models for each stats entry.
  stats_value_context_ = json_stats_map_->newKey("stats");
  json_stats_array_ = json_streamer_.newArray();
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
  json_streamer_.mapEntries({{"name", Json::Streamer::quote(Json::sanitize(name_buffer_, name))},
                             {"value", Json::Streamer::number(value)}});
  drainIfNeeded(response);
}

// Buffers a JSON fragment for a text-readout stat, flushing to the response
// buffer once we exceed JsonStatsFlushCount stats.
void StatsJsonRender::generate(Buffer::Instance& response, const std::string& name,
                               const std::string& value) {
  ASSERT(!histograms_initialized_);
  json_streamer_.mapEntries(
      {{"name", Json::Streamer::quote(Json::sanitize(name_buffer_, name))},
       {"value", Json::Streamer::quote(Json::sanitize(value_buffer_, value))}});
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

  json_histogram_array_->newEntry();
  switch (histogram_buckets_mode_) {
  case Utility::HistogramBucketsMode::NoBuckets: {
    Json::Streamer::Map& map = json_streamer_.newMap();
    map.newEntries({{"name", Json::Streamer::quote(Json::sanitize(name_buffer_, name))}});
    map.newKey("values");
    populatePercentiles(histogram);
    json_streamer_.pop(map);
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

void StatsJsonRender::populateSupportedPercentiles() {
  // auto x100 = [](double fraction) -> std::string { return Json::Streamer::number(fraction * 100);
  // }; auto view = [](const std::string& str) -> absl::string_view { return str; };
  Stats::HistogramStatisticsImpl empty_statistics;
  std::vector<double> supported = empty_statistics.supportedQuantiles();
  std::vector<std::string> supported_strings(supported.size());
  std::vector<absl::string_view> views(supported.size());
  uint32_t i = 0;
  for (double quantile : supported) {
    supported_strings[i] = Json::Streamer::number(quantile * 100);
    views[i] = supported_strings[i];
    ++i;
  }
  // std::transform(supported.begin(), supported.end(), supported_strings.end(), x100);
  // std::transform(supported_strings.begin(), supported_strings.end(), views.end(), view);
  json_streamer_.arrayEntries(views);
}

void StatsJsonRender::populatePercentiles(const Stats::ParentHistogram& histogram) {
  Json::Streamer::Array& array = json_streamer_.newArray();
  std::vector<double> totals = histogram.cumulativeStatistics().computedQuantiles(),
                      intervals = histogram.intervalStatistics().computedQuantiles();
  uint32_t min_size = std::min(totals.size(), intervals.size());
  ASSERT(totals.size() == min_size);
  ASSERT(intervals.size() == min_size);
  for (uint32_t i = 0; i < min_size; ++i) {
    json_streamer_.mapEntries({{"cumulative", Json::Streamer::number(totals[i])},
                               {"interval", Json::Streamer::number(intervals[i])}});
  }
  json_streamer_.pop(array);
};

void StatsJsonRender::renderHistogramStart() {
  histograms_initialized_ = true;
  json_stats_array_->newEntry();
  json_histogram_map1_ = json_streamer_.newMap();
  Json::Streamer::Map::ValuePtr hist_value_context = json_histogram_map1_->newKey("histograms");

  switch (histogram_buckets_mode_) {
  case Utility::HistogramBucketsMode::Detailed:
    json_histogram_map2_ = json_streamer_.newMap();
    {
      Json::Streamer::Map::ValuePtr value_context =
          json_histogram_map2_->newKey("supported_percentiles");
      populateSupportedPercentiles();
      // json_histogram_map2_->endValue();
    }
    {
      Json::Streamer::Map::ValuePtr value_context = json_histogram_map2_->newKey("details");
      json_histogram_array_ = json_streamer_.newArray();
      // json_histogram_map2_->endValue();
    }
    break;
  case Utility::HistogramBucketsMode::NoBuckets:
    json_histogram_map2_ = json_streamer_.newMap();
    {
      Json::Streamer::Map::ValuePtr value_context =
          json_histogram_map2_->newKey("supported_quantiles");
      populateSupportedPercentiles();
      // json_histogram_map2_->endValue();
    }
    {
      Json::Streamer::Map::ValuePtr value_context =
          json_histogram_map2_->newKey("computed_quantiles");
      json_histogram_array_ = json_streamer_.newArray();
    }
    break;
  case Utility::HistogramBucketsMode::Cumulative:
  case Utility::HistogramBucketsMode::Disjoint:
    json_histogram_array_ = json_streamer_.newArray();
    break;
  }
}

void StatsJsonRender::generateHistogramDetail(const std::string& name,
                                              const Stats::ParentHistogram& histogram) {
  // Now we produce the stream-able histogram records, without using the json intermediate
  // representation or serializer.
  Json::Streamer::Map& map = json_streamer_.newMap();
  map.newEntries({{"name", absl::StrCat("\"", Json::sanitize(name_buffer_, name), "\"")}});

  {
    Json::Streamer::Map::ValuePtr value_context = map.newKey("totals");
    populateBucketsVerbose(histogram.detailedTotalBuckets());
  }
  {
    Json::Streamer::Map::ValuePtr value_context = map.newKey("intervals");
    populateBucketsVerbose(histogram.detailedIntervalBuckets());
    // map.endValue();
  }
  {
    Json::Streamer::Map::ValuePtr value_context = map.newKey("percentiles");
    populatePercentiles(histogram);
    // map.endValue();
  }
}

void StatsJsonRender::populateBucketsTerse(
    const std::vector<Stats::ParentHistogram::Bucket>& buckets) {
  Json::Streamer::Array& buckets_array = json_streamer_.newArray();
  for (const Stats::ParentHistogram::Bucket& bucket : buckets) {
    buckets_array.newEntry();
    Json::Streamer::Array& array = json_streamer_.newArray();
    array.newEntries({Json::Streamer::number(bucket.lower_bound_),
                      Json::Streamer::number(bucket.width_),
                      Json::Streamer::number(bucket.count_)});
    json_streamer_.pop(array);
  }
  json_streamer_.pop(buckets_array);
}

void StatsJsonRender::populateBucketsVerbose(
    const std::vector<Stats::ParentHistogram::Bucket>& buckets) {
  Json::Streamer::Array& buckets_array = json_streamer_.newArray();
  for (const Stats::ParentHistogram::Bucket& bucket : buckets) {
    json_streamer_.mapEntries({{"lower_bound", Json::Streamer::number(bucket.lower_bound_)},
                               {"width", Json::Streamer::number(bucket.width_)},
                               {"count", Json::Streamer::number(bucket.count_)}});
  }
  json_streamer_.pop(buckets_array);
}

// Since histograms are buffered (see above), the finalize() method generates
// all of them.
void StatsJsonRender::finalize(Buffer::Instance& response) {
  stats_value_context_.reset();
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

  Json::Streamer::Map& map = json_streamer_.newMap();
  {
    Json::Streamer::Map::ValuePtr value_context = map.newKey("name");
    value_context->addSanitized(name);
  }
  // map.newSanitizedValue(name);
  {
    Json::Streamer::Map::ValuePtr value_context = map.newKey("buckets");
    Json::Streamer::Array& buckets = json_streamer_.newArray();
    for (uint32_t i = 0; i < min_size; ++i) {
      buckets.newEntry();
      Json::Streamer::Map& bucket_map = json_streamer_.newMap();
      // using NameValue = Json::Streamer::Map::NameValue;
      bucket_map.newEntries({{"upper_bound", Json::Streamer::number(supported_buckets[i])},
                             {"interval", Json::Streamer::number(interval_buckets[i])},
                             {"cumulative", Json::Streamer::number(cumulative_buckets[i])}});

      json_streamer_.pop(bucket_map);
    }
    // map.endValue();
    json_streamer_.pop(buckets);
  }
  json_streamer_.pop(map);
}

} // namespace Server
} // namespace Envoy
