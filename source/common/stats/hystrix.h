#include <map>
#include <memory>
#include <vector>

#include "envoy/server/admin.h"
#include "envoy/server/instance.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Stats {

typedef std::vector<uint64_t> RollingStats;
typedef std::map<std::string, RollingStats> RollingStatsMap;

// Consider implement the HystrixStats as a sink to have access to histograms data
class Hystrix {

public:
  Hystrix() : current_index_(DEFAULT_NUM_OF_BUCKETS), num_of_buckets_(DEFAULT_NUM_OF_BUCKETS + 1){};

  Hystrix(uint64_t num_of_buckets)
      : current_index_(num_of_buckets), num_of_buckets_(num_of_buckets + 1){};

  /**
   * Add new value to top of rolling window, pushing out the oldest value
   */
  void pushNewValue(std::string key, uint64_t value);

  /**
   * increment pointer of next value to add to rolling window
   */
  void incCounter() { current_index_ = (current_index_ + 1) % num_of_buckets_; }

  /**
   * Generate the streams to be sent to hystrix dashboard
   */
  void getClusterStats(std::stringstream& ss, std::string cluster_name,
                       uint64_t max_concurrent_requests, uint64_t reporting_hosts);

  /**
   * Get value of the sampling buckets
   */
  static uint64_t GetRollingWindowIntervalInMs() {
    return static_cast<const uint64_t>(ROLLING_WINDOW_IN_MS / DEFAULT_NUM_OF_BUCKETS);
  }

  /**
   * Get value of the keep alive ping interval
   */
  static uint64_t GetPingIntervalInMs() { return PING_INTERVAL_IN_MS; }

  void updateRollingWindowMap(std::map<std::string, uint64_t> current_stat_values,
                              std::string cluster_name);

  /**
   * clear map
   */
  void resetRollingWindow();

  /**
   * return string represnting current state of the map. for DEBUG.
   */
  std::string printRollingWindow();

private:
  /**
   * Get the statistic's value change over the rolling window time frame
   */
  uint64_t getRollingValue(std::string cluster_name, std::string stats);

  /**
   * Format the given key and std::string value to "key"="value", and adding to the stringstream
   */
  void addStringToStream(std::string key, std::string value, std::stringstream& info);

  /**
   * Format the given key and uint64_t value to "key"=<string of uint64_t>, and adding to the
   * stringstream
   */
  void addIntToStream(std::string key, uint64_t value, std::stringstream& info);

  /**
   * Format the given key and value to "key"=value, and adding to the stringstream
   */
  void addInfoToStream(std::string key, std::string value, std::stringstream& info);

  /**
   * generate HystrixCommand event stream
   */
  void addHystrixCommand(std::stringstream& ss, std::string cluster_name,
                         uint64_t max_concurrent_requests, uint64_t reporting_hosts);

  /**
   * generate HystrixThreadPool event stream
   */
  void addHystrixThreadPool(std::stringstream& ss, std::string cluster_name, uint64_t queue_size,
                            uint64_t reporting_hosts);

  RollingStatsMap rolling_stats_map_;
  uint64_t current_index_;
  uint64_t num_of_buckets_;
  // TODO(trabetti): May want to make this configurable via config file
  static const uint64_t DEFAULT_NUM_OF_BUCKETS = 10;
  static const uint64_t ROLLING_WINDOW_IN_MS = 10000;
  static const uint64_t PING_INTERVAL_IN_MS = 3000;
};

typedef std::unique_ptr<Hystrix> HystrixPtr;

/**
 * This class contains data which will be sent from admin filter to a hystrix_event_stream handler
 * and build a class which contains the relevant data.
 */
class HystrixHandlerInfoImpl : public Server::HandlerInfo {
public:
  HystrixHandlerInfoImpl(Http::StreamDecoderFilterCallbacks* callbacks) : callbacks_(callbacks) {}
  virtual ~HystrixHandlerInfoImpl(){};

  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

namespace HystrixNameSpace {

class HystrixSink : public Sink {
public:
  HystrixSink(Server::Instance& server);
  void beginFlush();
  void flushCounter(const Counter& counter, uint64_t delta);
  void flushGauge(const Gauge&, uint64_t){};
  void endFlush();
  void onHistogramComplete(const Histogram& histogram, uint64_t value) {
    std::cout << "histogram complete: " << histogram.name() << ", value: " << std::to_string(value)
              << std::endl;
  };

  /**
   * register a new connection
   */
  void registerConnection(Http::StreamDecoderFilterCallbacks* callbacks_);
  /**
   * remove registered connection
   */
  void unregisterConnection();

private:
  Stats::HystrixPtr stats_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  Server::Instance* server_;
  std::map<std::string, uint64_t> current_stat_values_;
};

typedef std::unique_ptr<HystrixSink> HystrixSinkPtr;

} // namespace HystrixNameSpace

} // namespace Stats
} // namespace Envoy
