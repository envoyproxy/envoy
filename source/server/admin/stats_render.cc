#include "source/server/admin/stats_render.h"

#include "source/common/json/json_sanitizer.h"
#include "source/common/stats/histogram_impl.h"

namespace {
constexpr absl::string_view JsonNameTag = "{\"name\":\"";
constexpr absl::string_view JsonValueTag = "\",\"value\":";
constexpr absl::string_view JsonValueTagQuote = "\",\"value\":\"";
constexpr absl::string_view JsonCloseBrace = "}";
constexpr absl::string_view JsonQuoteCloseBrace = "\"}";
} // namespace

namespace Envoy {

using ProtoMap = Protobuf::Map<std::string, ProtobufWkt::Value>;

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
  }
}

void StatsTextRender::finalize(Buffer::Instance&) {}

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
    : histogram_buckets_mode_(params.histogram_buckets_mode_) {
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  // We don't create a JSON data model for the entire stats output, as that
  // makes streaming difficult. Instead we emit the preamble in the
  // constructor here, and create json models for each stats entry.
  response.add("{\"stats\":[");
  histogram_array_ = std::make_unique<ProtobufWkt::ListValue>();
}

// Buffers a JSON fragment for a numeric stats, flushing to the response
// buffer once we exceed JsonStatsFlushCount stats.
void StatsJsonRender::generate(Buffer::Instance& response, const std::string& name,
                               uint64_t value) {
  response.addFragments({delim_, JsonNameTag, Json::sanitize(name_buffer_, name), JsonValueTag,
                         std::to_string(value), JsonCloseBrace});
  delim_ = ",";
}

// Buffers a JSON fragment for a text-readout stat, flushing to the response
// buffer once we exceed JsonStatsFlushCount stats.
void StatsJsonRender::generate(Buffer::Instance& response, const std::string& name,
                               const std::string& value) {
  response.addFragments({delim_, JsonNameTag, Json::sanitize(name_buffer_, name), JsonValueTagQuote,
                         Json::sanitize(value_buffer_, value), JsonQuoteCloseBrace});
  delim_ = ",";
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
  switch (histogram_buckets_mode_) {
  case Utility::HistogramBucketsMode::NoBuckets:
    summarizeBuckets(name, histogram);
    break;
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
  }
}

// Since histograms are buffered (see above), the finalize() method generates
// all of them.
void StatsJsonRender::finalize(Buffer::Instance& response) {
  if (histogram_array_->values_size() > 0) {
    ProtoMap& histograms_obj_container_fields = *histograms_obj_container_.mutable_fields();
    if (found_used_histogram_) {
      ASSERT(histogram_buckets_mode_ == Utility::HistogramBucketsMode::NoBuckets);
      ProtoMap& histograms_obj_fields = *histograms_obj_.mutable_fields();
      histograms_obj_fields["computed_quantiles"].set_allocated_list_value(
          histogram_array_.release());
      histograms_obj_container_fields["histograms"] = ValueUtil::structValue(histograms_obj_);
    } else {
      ASSERT(histogram_buckets_mode_ != Utility::HistogramBucketsMode::NoBuckets);
      histograms_obj_container_fields["histograms"].set_allocated_list_value(
          histogram_array_.release());
    }
    auto str = MessageUtil::getJsonStringFromMessageOrDie(
        ValueUtil::structValue(histograms_obj_container_), false /* pretty */, true);

    // Protobuf json serialization can yield an empty string (printing an
    // untrappable error message to stdout) if it receives an invalid input, so
    // we exclude that here.
    if (!str.empty()) {
      response.addFragments({delim_, str});
    }
  }
  response.add("]}");
}

// Summarizes the buckets in the specified histogram, collecting JSON objects.
// Note, we do not flush this buffer to the network when it grows large, and
// if this becomes an issue it should be possible to do, noting that we are
// one or two levels nesting below the list of scalar stats due to the Envoy
// stats json schema, where histograms are grouped together.
void StatsJsonRender::summarizeBuckets(const std::string& name,
                                       const Stats::ParentHistogram& histogram) {
  if (!found_used_histogram_) {
    // It is not possible for the supported quantiles to differ across histograms, so it is ok
    // to send them once.
    Stats::HistogramStatisticsImpl empty_statistics;
    ProtoMap& histograms_obj_fields = *histograms_obj_.mutable_fields();
    ProtobufWkt::ListValue* supported_quantile_array =
        histograms_obj_fields["supported_quantiles"].mutable_list_value();

    for (double quantile : empty_statistics.supportedQuantiles()) {
      *supported_quantile_array->add_values() = ValueUtil::numberValue(quantile * 100);
    }

    found_used_histogram_ = true;
  }

  ProtobufWkt::Struct computed_quantile;
  ProtoMap& computed_quantile_fields = *computed_quantile.mutable_fields();
  computed_quantile_fields["name"] = ValueUtil::stringValue(name);

  ProtobufWkt::ListValue* computed_quantile_value_array =
      computed_quantile_fields["values"].mutable_list_value();
  const Stats::HistogramStatistics& interval_statistics = histogram.intervalStatistics();
  const std::vector<double>& computed_quantiles = interval_statistics.computedQuantiles();
  const std::vector<double>& cumulative_quantiles =
      histogram.cumulativeStatistics().computedQuantiles();
  const size_t min_size = std::min({computed_quantiles.size(), cumulative_quantiles.size(),
                                    interval_statistics.supportedQuantiles().size()});
  ASSERT(min_size == computed_quantiles.size());
  ASSERT(min_size == cumulative_quantiles.size());

  for (size_t i = 0; i < min_size; ++i) {
    ProtobufWkt::Struct computed_quantile_value;
    ProtoMap& computed_quantile_value_fields = *computed_quantile_value.mutable_fields();
    const auto& interval = computed_quantiles[i];
    const auto& cumulative = cumulative_quantiles[i];
    computed_quantile_value_fields["interval"] =
        std::isnan(interval) ? ValueUtil::nullValue() : ValueUtil::numberValue(interval);
    computed_quantile_value_fields["cumulative"] =
        std::isnan(cumulative) ? ValueUtil::nullValue() : ValueUtil::numberValue(cumulative);

    *computed_quantile_value_array->add_values() = ValueUtil::structValue(computed_quantile_value);
  }
  *histogram_array_->add_values() = ValueUtil::structValue(computed_quantile);
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

  ProtobufWkt::Struct histogram_obj;
  ProtoMap& histogram_obj_fields = *histogram_obj.mutable_fields();
  histogram_obj_fields["name"] = ValueUtil::stringValue(name);
  ProtobufWkt::ListValue* bucket_array = histogram_obj_fields["buckets"].mutable_list_value();

  for (size_t i = 0; i < min_size; ++i) {
    ProtobufWkt::Struct bucket;
    ProtoMap& bucket_fields = *bucket.mutable_fields();
    bucket_fields["upper_bound"] = ValueUtil::numberValue(supported_buckets[i]);

    // ValueUtil::numberValue does unnecessary conversions from uint64_t values to doubles.
    bucket_fields["interval"] = ValueUtil::numberValue(interval_buckets[i]);
    bucket_fields["cumulative"] = ValueUtil::numberValue(cumulative_buckets[i]);
    *bucket_array->add_values() = ValueUtil::structValue(bucket);
  }
  *histogram_array_->add_values() = ValueUtil::structValue(histogram_obj);
}

} // namespace Server
} // namespace Envoy
