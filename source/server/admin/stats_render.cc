#include "source/server/admin/stats_render.h"

#include "source/common/json/json_sanitizer.h"
#include "source/common/stats/histogram_impl.h"

#include "absl/strings/str_format.h"

namespace {
constexpr absl::string_view JsonNameTag = "{\"name\":\"";
constexpr absl::string_view JsonValueTag = "\",\"value\":";
constexpr absl::string_view JsonValueTagQuote = "\",\"value\":\"";
constexpr absl::string_view JsonCloseBrace = "}";
constexpr absl::string_view JsonQuoteCloseBrace = "\"}";
} // namespace

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
  case Utility::HistogramBucketsMode::Combined:
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
    : histogram_buckets_mode_(params.histogram_buckets_mode_),
      json_streamer_(response),
      json_stats_map_(json_streamer_.newMap()) {
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  // We don't create a JSON data model for the stats output, as that makes
  // streaming difficult. Instead we emit the preamble in the constructor here,
  // and create json models for each stats entry.
  json_stats_map_->newKey("stats");
  json_stats_array_ = json_streamer_.newArray();
}

// Buffers a JSON fragment for a numeric stats, flushing to the response
// buffer once we exceed JsonStatsFlushCount stats.
void StatsJsonRender::generate(Buffer::Instance&, const std::string& name,
                               uint64_t value) {
  ASSERT(!histograms_initialized_);
  json_stats_array_->newEntry();
  json_streamer_.addFragments({JsonNameTag, Json::sanitize(name_buffer_, name), JsonValueTag,
      std::to_string(value), JsonCloseBrace});
}

// Buffers a JSON fragment for a text-readout stat, flushing to the response
// buffer once we exceed JsonStatsFlushCount stats.
void StatsJsonRender::generate(Buffer::Instance&, const std::string& name,
                               const std::string& value) {
  ASSERT(!histograms_initialized_);
  json_stats_array_->newEntry();
  json_streamer_.addFragments({JsonNameTag, Json::sanitize(name_buffer_, name), JsonValueTagQuote,
      Json::sanitize(value_buffer_, value), JsonQuoteCloseBrace});
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
void StatsJsonRender::generate(Buffer::Instance&, const std::string& name,
                               const Stats::ParentHistogram& histogram) {
  if (!histograms_initialized_) {
    renderHistogramStart();
  }

  switch (histogram_buckets_mode_) {
    case Utility::HistogramBucketsMode::NoBuckets: {
      std::vector<double> totals = histogram.cumulativeStatistics().computedQuantiles(),
                       intervals = histogram.intervalStatistics().computedQuantiles();
      uint32_t min_size = std::min(totals.size(), intervals.size());
      ASSERT(totals.size() == min_size);
      ASSERT(intervals.size() == min_size);
      absl::string_view prefix = "";
      json_histogram_array_->newEntry();
      json_streamer_.addFragments({
          "{\"name\":\"", Json::sanitize(name_buffer_, name),
          "\",\"values\":["});
      for (uint32_t i = 0; i < min_size; ++i) {
        json_streamer_.addCopy(absl::StrCat(prefix, "{\"cumulative\":", totals[i], ",\"interval\":",
                                            intervals[i], "}"));
        prefix = ",";
      }
      json_streamer_.addNoCopy("]}");
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
    case Utility::HistogramBucketsMode::Combined:
      generateHistogramDetail(name, histogram);
      break;
  }
}

void StatsJsonRender::populateSupportedPercentiles() {
  auto x100 = [](double fraction) -> double { return fraction * 100; };
  Stats::HistogramStatisticsImpl empty_statistics;
  std::vector<double> supported = empty_statistics.supportedQuantiles();
  std::transform(supported.begin(), supported.end(), supported.begin(), x100);
  json_streamer_.addFragments({"[", absl::StrJoin(supported, ","), "]"});
}

void StatsJsonRender::renderHistogramStart() {
  histograms_initialized_ = true;
  json_stats_array_->newEntry();

  if (histogram_buckets_mode_ != Utility::HistogramBucketsMode::Combined) {
    json_histogram_map1_ = json_streamer_.newMap();
    json_histogram_map1_->newKey("histograms");
  }

  switch (histogram_buckets_mode_) {
    case Utility::HistogramBucketsMode::Detailed:
      json_histogram_map2_ = json_streamer_.newMap();
      json_histogram_map2_->newKey("supported_percentiles");
      populateSupportedPercentiles();
      json_histogram_map2_->newKey("details");
      json_histogram_array_ = json_streamer_.newArray();
      break;
    case Utility::HistogramBucketsMode::Combined: {
      // 'Combined' histogram mode results in a stat of type
      // 'supported_percentileds' being created once, covering all
      // histograms. All other histograms are emitted at the same logical level
      // as counter, gauges, or text readouts, simplifying the hierarchy, but
      // making the 'combined' mode work somewhat different from the others.
      //
      // Both the single 'supported_percentiles'.
      Json::Streamer::Map& map = json_streamer_.newMap();
      map.newKey("supported_percentiles");
      populateSupportedPercentiles();
      json_streamer_.pop(map);
      json_stats_array_->newEntry();
      json_histogram_map1_ = json_streamer_.newMap();
      json_streamer_.addNoCopy("\"histograms\":");
      json_histogram_array_ = json_streamer_.newArray();
      break;
    }
    case Utility::HistogramBucketsMode::NoBuckets:
      json_histogram_map2_ = json_streamer_.newMap();
      json_histogram_map2_->newKey("supported_quantiles");
      populateSupportedPercentiles();
      json_histogram_map2_->newKey("computed_quantiles");
      json_histogram_array_ = json_streamer_.newArray();
      break;
    case Utility::HistogramBucketsMode::Cumulative:
    case Utility::HistogramBucketsMode::Disjoint:
      json_histogram_array_ = json_streamer_.newArray();
      break;
  }
}

void StatsJsonRender::generateHistogramDetail(const std::string& name,
                                              const Stats::ParentHistogram& histogram) {
  const bool combined = !json_histogram_array_.has_value();
  if (combined) {
    json_stats_array_->newEntry(); // Histograms are at the same hierarchical level as counters and gauges.
  }

  // Now we produce the streamable histogram records, without using the json intermediate
  // representation or serializer.
  json_streamer_.addFragments({"{\"name\":\"", Json::sanitize(name_buffer_, name),
      "\",\"totals\":["});
  populateBuckets(histogram.detailedTotalBuckets());
  json_streamer_.addFragments({"],\"intervals\":["});
  populateBuckets(histogram.detailedIntervalBuckets());
  std::vector<double> totals = histogram.cumulativeStatistics().computedQuantiles(),
                   intervals = histogram.intervalStatistics().computedQuantiles();
  json_streamer_.addFragments({"],"
      "\"percentiles\":[", absl::StrJoin(totals, ","), "]"
      //"\"cumulative_percentiles\":[", absl::StrJoin(intervals, ","), "]"
      "}"});
}

void StatsJsonRender::populateBuckets(const std::vector<Stats::ParentHistogram::Bucket>& buckets) {
  absl::string_view prefix = "";
  for (const Stats::ParentHistogram::Bucket& bucket : buckets) {
    json_streamer_.addCopy(absl::StrFormat("%s[%g,%g,%g]", prefix, bucket.lower_bound_, bucket.width_,
                                           bucket.count_));
    prefix = ",";
  }
}

// Since histograms are buffered (see above), the finalize() method generates
// all of them.
void StatsJsonRender::finalize(Buffer::Instance&) {
  /*
    json_histogram_array_.reset();
    json_histogram_map2_.reset();
    json_histogram_map1_.reset();
    json_stats_array_.reset();
    json_stats_map_.reset();
  */
  json_streamer_.clear();
  //json_streamer_.flush();
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
  map.newKey("name");
  json_streamer_.addSanitized(name);
  map.newKey("buckets");
  Json::Streamer::Array& buckets = json_streamer_.newArray();
  for (uint32_t i = 0; i < min_size; ++i) {
    buckets.newEntry();
    json_streamer_.addCopy(absl::StrCat("{\"upper_bound\":", supported_buckets[i],
                                        ",\"interval\":", interval_buckets[i],
                                        ",\"cumulative\":", cumulative_buckets[i],
                                        "}"));
  }
  json_streamer_.pop(buckets);
  json_streamer_.pop(map);
}

} // namespace Server
} // namespace Envoy
