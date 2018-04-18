#include <map>
#include <memory>
#include <vector>

#include "envoy/server/admin.h"
#include "envoy/server/instance.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {

typedef std::vector<uint64_t> RollingWindow;
typedef std::map<const std::string, RollingWindow> RollingStatsMap;

class HystrixStatCache : public Logger::Loggable<Logger::Id::hystrix> {
public:
  HystrixStatCache()
      : current_index_(DEFAULT_NUM_OF_BUCKETS), window_size_(DEFAULT_NUM_OF_BUCKETS + 1){};

  HystrixStatCache(uint64_t num_of_buckets)
      : current_index_(num_of_buckets), window_size_(num_of_buckets + 1){};

  /**
   * Add new value to top of rolling window, pushing out the oldest value.
   */
  void pushNewValue(const std::string& key, uint64_t value);

  /**
   * Increment pointer of next value to add to rolling window.
   */
  void incCounter() { current_index_ = (current_index_ + 1) % window_size_; }

  /**
   * Generate the streams to be sent to hystrix dashboard.
   */
  void getClusterStats(std::stringstream& ss, absl::string_view cluster_name,
                       uint64_t max_concurrent_requests, uint64_t reporting_hosts,
                       uint64_t rolling_window);

  /**
   * Calculate values needed to create the stream and write into the map.
   */
  void updateRollingWindowMap(std::map<std::string, uint64_t> current_stat_values,
                              const std::string cluster_name);

  /**
   * Clear map.
   */
  void resetRollingWindow();

  /**
   * Return string represnting current state of the map. for DEBUG.
   */
  const std::string printRollingWindow() const;

  /**
   * Get the statistic's value change over the rolling window time frame.
   */
  uint64_t getRollingValue(absl::string_view cluster_name, absl::string_view stat);

private:
  /**
   * Format the given key and absl::string_view value to "key"="value", and adding to the
   * stringstream.
   */
  void addStringToStream(absl::string_view key, absl::string_view value, std::stringstream& info);

  /**
   * Format the given key and uint64_t value to "key"=<string of uint64_t>, and adding to the
   * stringstream.
   */
  void addIntToStream(absl::string_view key, uint64_t value, std::stringstream& info);

  /**
   * Format the given key and value to "key"=value, and adding to the stringstream.
   */
  void addInfoToStream(absl::string_view key, absl::string_view value, std::stringstream& info);

  /**
   * Generate HystrixCommand event stream.
   */
  void addHystrixCommand(std::stringstream& ss, absl::string_view cluster_name,
                         uint64_t max_concurrent_requests, uint64_t reporting_hosts,
                         uint64_t rolling_window);

  /**
   * Generate HystrixThreadPool event stream.
   */
  void addHystrixThreadPool(std::stringstream& ss, absl::string_view cluster_name,
                            uint64_t queue_size, uint64_t reporting_hosts, uint64_t rolling_window);

  /**
   * Building lookup name map for all specific cluster values.
   */
  void CreateCounterNameLookupForCluster(const std::string& cluster_name);

  RollingStatsMap rolling_stats_map_;
  uint64_t current_index_;
  const uint64_t window_size_;
  // TODO(trabetti): do we want this to be configurable through the HystrixSink in config file?
  static const uint64_t DEFAULT_NUM_OF_BUCKETS = 10;
  std::map<std::string, std::map<std::string, std::string>> counter_name_lookup;
};

typedef std::unique_ptr<HystrixStatCache> HystrixPtr;

namespace Hystrix {

class HystrixSink : public Stats::Sink, public Logger::Loggable<Logger::Id::hystrix> {
public:
  HystrixSink(Server::Instance& server);
  Http::Code handlerHystrixEventStream(absl::string_view, Http::HeaderMap& response_headers,
                                       Buffer::Instance&, Server::AdminFilter& admin_filter);
  void beginFlush() override;
  void flushCounter(const Stats::Counter& counter, uint64_t delta) override;
  void flushGauge(const Stats::Gauge&, uint64_t) override{};
  void endFlush() override;
  void onHistogramComplete(const Stats::Histogram&, uint64_t) override{};

  // TODO (@trabetti) : support multiple connections
  /**
   * register a new connection
   */
  void registerConnection(Http::StreamDecoderFilterCallbacks* callbacks_to_register);
  /**
   * remove registered connection
   */
  void unregisterConnection(Http::StreamDecoderFilterCallbacks* callbacks_to_remove);
  HystrixStatCache& getStats() { return *stats_; }

private:
  HystrixPtr stats_;
  std::vector<Http::StreamDecoderFilterCallbacks*> callbacks_list_{};
  Server::Instance* server_;
  std::map<std::string, uint64_t> current_stat_values_;
};

typedef std::unique_ptr<HystrixSink> HystrixSinkPtr;

} // namespace Hystrix
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
