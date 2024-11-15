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

  /**
   * Streams the supported percentiles into a JSON array. Note that no histogram
   * context is provided for this; this is a static property of the binary.
   *
   * @param array the json streaming array array to stream into.
   */
  static void populateSupportedPercentiles(Json::BufferStreamer::Array& array);

  /**
   * Streams detail about the provided histogram into the provided JSON map.
   *
   * @param name the name of the histogram (usually the same as histogram.name(),
   *             but is passed in explicitly because it may already have been
   *             computed when filtering stats, and it is somewhat expensive
   *             to compute the name.
   * @param histogram the histogram to stream
   * @param map the json map to stream into.
   *
   */
  static void generateHistogramDetail(const std::string& name,
                                      const Stats::ParentHistogram& histogram,
                                      Json::BufferStreamer::Map& map);

private:
  // Collects the buckets from the specified histogram.
  void collectBuckets(const std::string& name, const Stats::ParentHistogram& histogram,
                      const std::vector<uint64_t>& interval_buckets,
                      const std::vector<uint64_t>& cumulative_buckets);

  static void populateBucketsVerbose(const std::vector<Stats::ParentHistogram::Bucket>& buckets,
                                     Json::BufferStreamer::Map& map);
  void renderHistogramStart();
  static void populatePercentiles(const Stats::ParentHistogram& histogram,
                                  Json::BufferStreamer::Map& map);

  // This function irons out an API mistake made when defining the StatsRender
  // interface. The issue is that callers can provide a response buffer when
  // constructing a StatsRender instance, but can switch to a different response
  // buffer when rendering specific values.
  //
  // The problem comes with JSON histograms where each histogram is nested in a
  // structure with other histograms, so if you switch buffers you need to
  // ensure the pending bytes in the previous buffer is fully
  // drained. Unfortunately this buffer-switching is used in practice.
  void drainIfNeeded(Buffer::Instance& response);

  const Utility::HistogramBucketsMode histogram_buckets_mode_;
  std::string name_buffer_;  // Used for Json::sanitize for names.
  std::string value_buffer_; // Used for Json::sanitize for text-readout values.
  bool histograms_initialized_{false};

  // Captures the hierarchy of maps and arrays used to render scalar stats and
  // histograms in various formats. This is separated in its own structure to
  // facilitate clearing these structures in the reverse order of how they were
  // created during finalize().
  //
  // Another option to consider: flattening the member variables of JsonContext
  // into StatsJsonRender, and ensure that Render objects are always destructed
  // before the output stream is considered complete.
  struct JsonContext {
    explicit JsonContext(Buffer::Instance& response);
    Json::BufferStreamer streamer_;
    Json::BufferStreamer::MapPtr stats_map_;
    Json::BufferStreamer::ArrayPtr stats_array_;
    Json::BufferStreamer::MapPtr histogram_map1_;
    Json::BufferStreamer::MapPtr histogram_map2_;
    Json::BufferStreamer::ArrayPtr histogram_array_;
  };
  std::unique_ptr<JsonContext> json_;
  Buffer::Instance& response_;
};

} // namespace Server
} // namespace Envoy
