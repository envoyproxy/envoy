#pragma once

#include "envoy/server/lifecycle_notifier.h"

#include "common/upstream/logical_dns_cluster.h"

#include "absl/base/call_once.h"
#include "extension_registry.h"
#include "library/common/envoy_mobile_main_common.h"
#include "library/common/http/dispatcher.h"
#include "library/common/types/c_types.h"

namespace Envoy {

class Engine {
public:
  /**
   * Constructor for a new engine instance.
   * @param callbacks, the callbacks to use for engine lifecycle monitoring.
   * @param config, the Envoy configuration to use when starting the instance.
   * @param log_level, the log level with which to configure the engine.
   * @param preferred_network, hook to obtain the preferred network for new streams.
   */
  Engine(envoy_engine_callbacks callbacks, const char* config, const char* log_level,
         std::atomic<envoy_network_t>& preferred_network);

  /**
   * Engine destructor.
   */
  ~Engine();

  /**
   * Accessor for the http dispatcher.
   * @return Http::Dispatcher&, the dispatcher being used by the engine.
   */
  Http::Dispatcher& httpDispatcher();

  /**
   * Increment a counter with a given string of elements and by the given count.
   * @param elements, joined elements of the timeseries.
   * @param tags, custom tags of the reporting stat.
   * @param count, amount to add to the counter.
   */
  envoy_status_t recordCounterInc(const std::string& elements, envoy_stats_tags tags,
                                  uint64_t count);

  /**
   * Set a gauge of a given string of elements with the given value.
   * @param elements, joined elements of the timeseries.
   * @param tags, custom tags of the reporting stat.
   * @param value, value to set to the gauge.
   */
  envoy_status_t recordGaugeSet(const std::string& elements, envoy_stats_tags tags, uint64_t value);

  /**
   * Add to the gauge with the given string of elements and by the given amount.
   * @param elements, joined elements of the timeseries.
   * @param tags, custom tags of the reporting stat.
   * @param amount, amount to add to the gauge.
   */
  envoy_status_t recordGaugeAdd(const std::string& elements, envoy_stats_tags tags,
                                uint64_t amount);

  /**
   * Subtract from the gauge with the given string of elements and by the given amount.
   * @param elements, joined elements of the timeseries.
   * @param tags, custom tags of the reporting stat.
   * @param amount, amount to subtract from the gauge.
   */
  envoy_status_t recordGaugeSub(const std::string& elements, envoy_stats_tags tags,
                                uint64_t amount);

  /**
   * Record a value for the histogram with the given string of elements and unit measurement
   * @param elements, joined elements of the timeseries.
   * @param tags, custom tags of the reporting stat.
   * @param value, value to add to the aggregated distribution of values for quantile calculations
   * @param unit_measure, the unit of measurement (e.g. milliseconds, bytes, etc.)
   */
  envoy_status_t recordHistogramValue(const std::string& elements, envoy_stats_tags tags,
                                      uint64_t value, envoy_histogram_stat_unit_t unit_measure);

private:
  envoy_status_t run(std::string config, std::string log_level);

  Stats::ScopePtr client_scope_;
  Stats::StatNameSetPtr stat_name_set_;
  envoy_engine_callbacks callbacks_;
  Thread::MutexBasicLockable mutex_;
  Thread::CondVar cv_;
  std::unique_ptr<Http::Dispatcher> http_dispatcher_;
  std::unique_ptr<MobileMainCommon> main_common_ GUARDED_BY(mutex_);
  Server::Instance* server_{};
  Server::ServerLifecycleNotifier::HandlePtr postinit_callback_handler_;
  Event::Dispatcher* event_dispatcher_;
  // main_thread_ should be destroyed first, hence it is the last member variable. Objects that
  // instructions scheduled on the main_thread_ need to have a longer lifetime.
  std::thread main_thread_;
};

} // namespace Envoy
