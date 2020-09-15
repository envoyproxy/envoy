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
   * @param count, amount to add to the counter.
   */
  void recordCounter(const std::string& elements, uint64_t count);

private:
  envoy_status_t run(std::string config, std::string log_level);

  Stats::ScopePtr client_scope_;
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
