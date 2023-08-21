#pragma once

#include "envoy/common/optref.h"
#include "envoy/server/admin.h"
#include "envoy/stats/stats.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/json/json_streamer.h"
#include "source/server/admin/stats_params.h"
#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

// Abstract class for rendering stats. Every method is called "generate"
// differing only by the data type, to facilitate templatized call-sites.
//
// There are currently Json and Text implementations of this interface, and in
// #19546 an HTML version will be added to provide a hierarchical view.
class StatsRender {
public:
  virtual ~StatsRender() = default;

  // Writes a fragment for a numeric value, for counters and gauges.
  virtual void generate(Buffer::Instance& response, const std::string& name, uint64_t value) PURE;

  // Writes a json fragment for a textual value, for text readouts.
  virtual void generate(Buffer::Instance& response, const std::string& name,
                        const std::string& value) PURE;

  // Writes a histogram value.
  virtual void generate(Buffer::Instance& response, const std::string& name,
                        const Stats::ParentHistogram& histogram) PURE;

  // Completes rendering any buffered data.
  virtual void finalize(Buffer::Instance& response) PURE;

  // Indicates that no stats for a particular type have been found.
  virtual void noStats(Buffer::Instance&, absl::string_view) {}
};

// Implements the Render interface for simple textual representation of stats.
class StatsTextRender : public StatsRender {
public:
  explicit StatsTextRender(const StatsParams& params);

  // StatsRender
  void generate(Buffer::Instance& response, const std::string& name, uint64_t value) override;
  void generate(Buffer::Instance& response, const std::string& name,
                const std::string& value) override;
  void generate(Buffer::Instance& response, const std::string& name,
                const Stats::ParentHistogram& histogram) override;
  void finalize(Buffer::Instance&) override;

private:
  // Computes disjoint buckets as text and adds them to the response buffer.
  void addDisjointBuckets(const std::string& name, const Stats::ParentHistogram& histogram,
                          Buffer::Instance& response);

  void addDetail(const std::vector<Stats::ParentHistogram::Bucket>& buckets,
                 Buffer::Instance& response);

  const Utility::HistogramBucketsMode histogram_buckets_mode_;
};

// Implements the Render interface for json output.
class StatsJsonRender : public StatsRender {
public:
  StatsJsonRender(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                  const StatsParams& params);

  // StatsRender
  void generate(Buffer::Instance& response, const std::string& name, uint64_t value) override;
  void generate(Buffer::Instance& response, const std::string& name,
                const std::string& value) override;
  void generate(Buffer::Instance&, const std::string& name,
                const Stats::ParentHistogram& histogram) override;
  void finalize(Buffer::Instance& response) override;

private:
  // Collects the buckets from the specified histogram.
  void collectBuckets(const std::string& name, const Stats::ParentHistogram& histogram,
                      const std::vector<uint64_t>& interval_buckets,
                      const std::vector<uint64_t>& cumulative_buckets);

  void generateHistogramDetail(const std::string& name, const Stats::ParentHistogram& histogram);
  void populateBucketsVerbose(const std::vector<Stats::ParentHistogram::Bucket>& buckets,
                              Json::Streamer::Map& map);
  void renderHistogramStart();
  void populateSupportedPercentiles(Json::Streamer::Map& map);
  void populatePercentiles(const Stats::ParentHistogram& histogram, Json::Streamer::Map& map);
  void drainIfNeeded(Buffer::Instance& response);

  const Utility::HistogramBucketsMode histogram_buckets_mode_;
  std::string name_buffer_;  // Used for Json::sanitize for names.
  std::string value_buffer_; // Used for Json::sanitize for text-readout values.
  bool histograms_initialized_{false};
  Json::Streamer json_streamer_;
  Json::Streamer::MapPtr json_stats_map_;
  Json::Streamer::Map::DeferredValuePtr stats_value_context_;
  Json::Streamer::ArrayPtr json_stats_array_;
  Json::Streamer::MapPtr json_histogram_map1_;
  Json::Streamer::MapPtr json_histogram_map2_;
  Json::Streamer::ArrayPtr json_histogram_array_;
  Buffer::Instance& response_;
};

} // namespace Server
} // namespace Envoy
