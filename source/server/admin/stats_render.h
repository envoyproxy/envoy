#include "envoy/stats/stats.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/server/admin/utils.h"

namespace {
constexpr uint64_t ChunkSize = 2 * 1000 * 1000;
} // namespace

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

  // Determines whether the current chunk is full.
  bool isChunkFull(Buffer::Instance& response) { return response.length() > ChunkSize; }

protected:
  using UInt64Vec = std::vector<uint64_t>;
};

// Implements the Render interface for simple textual representation of stats.
class StatsTextRender : public StatsRender {
public:
  explicit StatsTextRender(Utility::HistogramBucketsMode histogram_buckets_mode)
      : histogram_buckets_mode_(histogram_buckets_mode) {}

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

  const Utility::HistogramBucketsMode histogram_buckets_mode_;
};

// Implements the Render interface for json output.
class StatsJsonRender : public StatsRender {
public:
  StatsJsonRender(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                  Utility::HistogramBucketsMode histogram_buckets_mode);

  // StatsRender
  void generate(Buffer::Instance& response, const std::string& name, uint64_t value) override;
  void generate(Buffer::Instance& response, const std::string& name,
                const std::string& value) override;
  void generate(Buffer::Instance&, const std::string& name,
                const Stats::ParentHistogram& histogram) override;
  void finalize(Buffer::Instance& response) override;

private:
  // Collects a scalar metric (text-readout, counter, or gauge) into an array of
  // stats, so they can all be serialized in one shot when a threshold is
  // reached. Serializing each one individually results in much worse
  // performance (see stats_handler_speed_test.cc).
  template <class Value>
  void addScalar(Buffer::Instance& response, const std::string& name, const Value& value);

  // Adds a JSON stat to our buffer, flushing to response every JsonStatsFlushCount stats.
  void addJson(Buffer::Instance& response, ProtobufWkt::Value json);

  // Flushes all stats that were buffered in addJson() above.
  void flushStats(Buffer::Instance& response);

  // Adds a json fragment of scalar stats to the response buffer, including a
  // "," delimiter if this is not the first fragment.
  void addStatAsRenderedJson(Buffer::Instance& response, absl::string_view json);

  // Summarizes the buckets in the specified histogram, collecting JSON objects.
  // Note, we do not flush this buffer to the network when it grows large, and
  // if this becomes an issue it should be possible to do, noting that we are
  // one or two levels nesting below the list of scalar stats due to the Envoy
  // stats json schema, where histograms are grouped together.
  void summarizeBuckets(const std::string& name, const Stats::ParentHistogram& histogram);

  // Collects the buckets from the specified histogram.
  void collectBuckets(const std::string& name, const Stats::ParentHistogram& histogram,
                      const UInt64Vec& interval_buckets, const UInt64Vec& cumulative_buckets);

  std::vector<ProtobufWkt::Value> stats_array_;
  ProtobufWkt::Struct histograms_obj_;
  ProtobufWkt::Struct histograms_obj_container_;
  std::vector<ProtobufWkt::Value> histogram_array_;
  bool found_used_histogram_{false};
  bool first_{true};
  const Utility::HistogramBucketsMode histogram_buckets_mode_;
};

} // namespace Server
} // namespace Envoy
