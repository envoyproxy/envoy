#pragma once

#include "envoy/server/lifecycle_notifier.h"
#include "envoy/stats/store.h"

#include "source/common/common/logger.h"

#include "absl/base/call_once.h"
#include "extension_registry.h"
#include "library/common/common/lambda_logger_delegate.h"
#include "library/common/engine_common.h"
#include "library/common/http/client.h"
#include "library/common/network/connectivity_manager.h"
#include "library/common/types/c_types.h"

namespace Envoy {

class Engine : public Logger::Loggable<Logger::Id::main> {
public:
  /**
   * Constructor for a new engine instance.
   * @param callbacks, the callbacks to use for engine lifecycle monitoring.
   * @param logger, the callbacks to use for engine logging.
   * @param event_tracker, the event tracker to use for the emission of events.
   */
  Engine(envoy_engine_callbacks callbacks, envoy_logger logger, envoy_event_tracker event_tracker);

  /**
   * Engine destructor.
   */
  ~Engine();

  /**
   * Run the engine with the provided configuration.
   * @param config, the Envoy bootstrap configuration to use.
   * @param log_level, the log level.
   */
  envoy_status_t run(std::string config, std::string log_level);
  envoy_status_t run(std::unique_ptr<Envoy::OptionsImplBase>&& options);

  /**
   * Immediately terminate the engine, if running.
   */
  envoy_status_t terminate();

  /**
   * Accessor for the provisional event dispatcher.
   * @return Event::ProvisionalDispatcher&, the engine dispatcher.
   */
  Event::ProvisionalDispatcher& dispatcher();

  /**
   * Accessor for the http client. Must be called from the dispatcher's context.
   * @return Http::Client&, the (default) http client.
   */
  Http::Client& httpClient();

  /**
   * Accessor for the network configuraator. Must be called from the dispatcher's context.
   * @return Network::ConnectivityManager&, the network connectivity_manager.
   */
  Network::ConnectivityManager& networkConnectivityManager();

  /**
   * Increment a counter with a given string of elements and by the given count.
   * @param elements, joined elements of the timeseries.
   * @param tags, custom tags of the reporting stat.
   * @param count, amount to add to the counter.
   */
  envoy_status_t recordCounterInc(const std::string& elements, envoy_stats_tags tags,
                                  uint64_t count);

  /**
   * Dump Envoy stats into the returned buffer
   * @returns a buffer with referenced stats dumped in Envoy's standard text format.
   */
  Buffer::OwnedImpl dumpStats();

  /**
   * Get cluster manager from the Engine.
   */
  Upstream::ClusterManager& getClusterManager();

  /*
   * Get the stats store from the Engine.
   */
  Stats::Store& getStatsStore();

private:
  envoy_status_t main(std::unique_ptr<Envoy::OptionsImplBase>&& options);
  static void logInterfaces(absl::string_view event,
                            std::vector<Network::InterfacePair>& interfaces);

  Event::Dispatcher* event_dispatcher_{};
  Stats::ScopeSharedPtr client_scope_;
  Stats::StatNameSetPtr stat_name_set_;
  envoy_engine_callbacks callbacks_;
  envoy_logger logger_;
  envoy_event_tracker event_tracker_;
  Assert::ActionRegistrationPtr assert_handler_registration_;
  Assert::ActionRegistrationPtr bug_handler_registration_;
  Thread::MutexBasicLockable mutex_;
  Thread::CondVar cv_;
  Http::ClientPtr http_client_;
  Network::ConnectivityManagerSharedPtr connectivity_manager_;
  Event::ProvisionalDispatcherPtr dispatcher_;
  // Used by the cerr logger to ensure logs don't overwrite each other.
  absl::Mutex log_mutex_;
  Logger::EventTrackingDelegatePtr log_delegate_ptr_{};
  Server::Instance* server_{};
  Server::ServerLifecycleNotifier::HandlePtr postinit_callback_handler_;
  // main_thread_ should be destroyed first, hence it is the last member variable. Objects with
  // instructions scheduled on the main_thread_ need to have a longer lifetime.
  std::thread main_thread_{}; // Empty placeholder to be populated later.
};

using EngineSharedPtr = std::shared_ptr<Engine>;
using EngineWeakPtr = std::weak_ptr<Engine>;

} // namespace Envoy
