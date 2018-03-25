#include <map>
#include <memory>
#include <vector>

#include "envoy/server/admin.h"
#include "envoy/server/instance.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Stats {

typedef std::vector<uint64_t> RollingStats;
typedef std::map<const std::string, RollingStats> RollingStatsMap;

// Consider implement the HystrixStats as a sink to have access to histograms data.
class Hystrix {

public:
  Hystrix() : current_index_(DEFAULT_NUM_OF_BUCKETS), num_of_buckets_(DEFAULT_NUM_OF_BUCKETS + 1){};

  Hystrix(uint64_t num_of_buckets)
      : current_index_(num_of_buckets), num_of_buckets_(num_of_buckets + 1){};

  /**
   * Add new value to top of rolling window, pushing out the oldest value.
   */
  void pushNewValue(const std::string& key, uint64_t value);

  /**
   * increment pointer of next value to add to rolling window.
   */
  void incCounter() { current_index_ = (current_index_ + 1) % num_of_buckets_; }

  /**
   * Generate the streams to be sent to hystrix dashboard.
   */
  void getClusterStats(std::stringstream& ss, absl::string_view cluster_name,
                       uint64_t max_concurrent_requests, uint64_t reporting_hosts);

  /**
   * Get value of the sampling buckets.
   */
  static uint64_t GetRollingWindowIntervalInMs() {
    return static_cast<uint64_t>(ROLLING_WINDOW_IN_MS / DEFAULT_NUM_OF_BUCKETS);
  }

  /**
   * Get value of the keep alive ping interval.
   */
  static uint64_t GetPingIntervalInMs() { return PING_INTERVAL_IN_MS; }

  void updateRollingWindowMap(Stats::Store& stats, absl::string_view cluster_name);

  /**
   * Clear map.
   */
  void resetRollingWindow();

  absl::string_view printRollingWindow() const;

private:
  /**
   * Get the statistic's value change over the rolling window time frame.
   */
  uint64_t getRollingValue(absl::string_view cluster_name, absl::string_view stats);

  /**
   * Format the given key and absl::string_view value to "key"="value", and adding to the stringstream.
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
   * generate HystrixCommand event stream
   */
  void addHystrixCommand(std::stringstream& ss, absl::string_view cluster_name,
                         uint64_t max_concurrent_requests, uint64_t reporting_hosts);

  /**
   * Generate HystrixThreadPool event stream.
   */
  void addHystrixThreadPool(std::stringstream& ss, absl::string_view cluster_name, uint64_t queue_size,
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
  HystrixHandlerInfoImpl(Http::StreamDecoderFilterCallbacks* callbacks)
      : stats_(new Stats::Hystrix()), data_timer_(nullptr), ping_timer_(nullptr),
        callbacks_(callbacks) {}
  virtual ~HystrixHandlerInfoImpl(){};
  virtual void Destroy();

  /**
   * HystrixHandlerInfoImpl includes statistics for hystrix API, timers for build (and send) data
   * and keep alive messages and the handler's callback.
   */
  Stats::HystrixPtr stats_;
  Event::TimerPtr data_timer_;
  Event::TimerPtr ping_timer_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

/**
 * Convert statistics from envoy format to hystrix format and prepare them and writes them to the
 * appropriate socket.
 */
class HystrixHandler {
public:
  static void HandleEventStream(HystrixHandlerInfoImpl* hystrix_handler_info,
                                Server::Instance& server);
  /**
   * Update counter and set values of upstream_rq statistics.
   * @param hystrix_handler_info is the data which is received in the hystrix handler from the admin
   * filter (callback, timers, statistics)
   * @param server contains envoy statistics
   */
  static void updateHystrixRollingWindow(HystrixHandlerInfoImpl* hystrix_handler_info,
                                         Server::Instance* server);
  /**
   * Builds a buffer of envoy statistics which will be sent to hystrix dashboard according to
   * hystrix API.
   * @param hystrix_handler_info is the data which is received in the hystrix handler from the admin
   * filter (callback, timers, statistics)
   * @param server contains envoy statistics*
   */
  static void prepareAndSendHystrixStream(HystrixHandlerInfoImpl* hystrix_handler_info,
                                          Server::Instance* server);
  /**
   * Sends a keep alive (ping) message to hystrix dashboard.
   * @param hystrix_handler_info is the data which is received in the hystrix handler from the admin
   * filter (callback, timers, statistics)
   */
  static void sendKeepAlivePing(HystrixHandlerInfoImpl* hystrix_handler_info);
};

} // namespace Stats
} // namespace Envoy
