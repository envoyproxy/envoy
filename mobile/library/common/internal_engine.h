#pragma once

#include "envoy/server/lifecycle_notifier.h"

#include "source/common/common/logger.h"
#include "source/common/common/macros.h"
#include "source/common/common/posix/thread_impl.h"
#include "source/common/common/thread.h"

#include "extension_registry.h"
#include "library/common/engine_common.h"
#include "library/common/engine_types.h"
#include "library/common/http/client.h"
#include "library/common/logger/logger_delegate.h"
#include "library/common/network/connectivity_manager.h"
#include "library/common/types/c_types.h"

namespace Envoy {

class InternalEngine : public Logger::Loggable<Logger::Id::main> {
public:
  /**
   * Constructor for a new engine instance.
   * @param callbacks, the callbacks to use for engine lifecycle monitoring.
   * @param logger, the callbacks to use for engine logging.
   * @param event_tracker, the event tracker to use for the emission of events.
   */
  InternalEngine(std::unique_ptr<EngineCallbacks> callbacks, std::unique_ptr<EnvoyLogger> logger,
                 std::unique_ptr<EnvoyEventTracker> event_tracker);

  /**
   * InternalEngine destructor.
   */
  ~InternalEngine();

  /**
   * Run the engine with the provided configuration.
   * @param config, the Envoy bootstrap configuration to use.
   * @param log_level, the log level.
   */
  envoy_status_t run(const std::string& config, const std::string& log_level);
  envoy_status_t run(std::shared_ptr<Envoy::OptionsImplBase> options);

  /**
   * Immediately terminate the engine, if running. Calling this function when
   * the engine has been terminated will result in `ENVOY_FAILURE`.
   */
  envoy_status_t terminate();

  /** Returns true if the Engine has been terminated; false otherwise. */
  bool isTerminated() const;

  /**
   * Accessor for the provisional event dispatcher.
   * @return Event::ProvisionalDispatcher&, the engine dispatcher.
   */
  Event::ProvisionalDispatcher& dispatcher();

  envoy_stream_t initStream();

  // These functions are wrappers around http client functions, which hand off
  // to http client functions of the same name after doing a dispatcher post
  // (thread context switch)
  envoy_status_t startStream(envoy_stream_t stream, envoy_http_callbacks bridge_callbacks,
                             bool explicit_flow_control) {
    return dispatcher_->post([&, stream, bridge_callbacks, explicit_flow_control]() {
      http_client_->startStream(stream, bridge_callbacks, explicit_flow_control);
    });
  }
  envoy_status_t sendHeaders(envoy_stream_t stream, envoy_headers headers, bool end_stream) {
    return dispatcher_->post([&, stream, headers, end_stream]() {
      http_client_->sendHeaders(stream, headers, end_stream);
    });
  }
  envoy_status_t readData(envoy_stream_t stream, size_t bytes_to_read) {
    return dispatcher_->post(
        [&, stream, bytes_to_read]() { http_client_->readData(stream, bytes_to_read); });
  }
  envoy_status_t sendData(envoy_stream_t stream, envoy_data data, bool end_stream) {
    return dispatcher_->post(
        [&, stream, data, end_stream]() { http_client_->sendData(stream, data, end_stream); });
  }
  envoy_status_t sendTrailers(envoy_stream_t stream, envoy_headers trailers) {
    return dispatcher_->post(
        [&, stream, trailers]() { http_client_->sendTrailers(stream, trailers); });
  }

  envoy_status_t cancelStream(envoy_stream_t stream) {
    return dispatcher_->post([&, stream]() { http_client_->cancelStream(stream); });
  }

  // These functions are wrappers around networkConnectivityManager functions, which hand off
  // to networkConnectivityManager after doing a dispatcher post (thread context switch)
  envoy_status_t setProxySettings(const char* host, const uint16_t port);
  envoy_status_t resetConnectivityState();
  envoy_status_t setPreferredNetwork(envoy_network_t network);

  /**
   * Increment a counter with a given string of elements and by the given count.
   * @param elements, joined elements of the timeseries.
   * @param tags, custom tags of the reporting stat.
   * @param count, amount to add to the counter.
   */
  envoy_status_t recordCounterInc(absl::string_view elements, envoy_stats_tags tags,
                                  uint64_t count);

  /**
   * Dumps Envoy stats into string. Returns an empty string when an error occurred.
   */
  std::string dumpStats();

  /**
   * Get cluster manager from the Engine.
   */
  Upstream::ClusterManager& getClusterManager();

  /*
   * Get the stats store from the Engine.
   */
  Stats::Store& getStatsStore();

private:
  GTEST_FRIEND_CLASS(InternalEngineTest, ThreadCreationFailed);

  InternalEngine(std::unique_ptr<EngineCallbacks> callbacks, std::unique_ptr<EnvoyLogger> logger,
                 std::unique_ptr<EnvoyEventTracker> event_tracker,
                 Thread::PosixThreadFactoryPtr thread_factory);

  envoy_status_t main(std::shared_ptr<Envoy::OptionsImplBase> options);
  static void logInterfaces(absl::string_view event,
                            std::vector<Network::InterfacePair>& interfaces);

  Thread::PosixThreadFactoryPtr thread_factory_;
  Event::Dispatcher* event_dispatcher_{};
  Stats::ScopeSharedPtr client_scope_;
  Stats::StatNameSetPtr stat_name_set_;
  std::unique_ptr<EngineCallbacks> callbacks_;
  std::unique_ptr<EnvoyLogger> logger_;
  std::unique_ptr<EnvoyEventTracker> event_tracker_;
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
  Thread::PosixThreadPtr main_thread_{nullptr}; // Empty placeholder to be populated later.
  bool terminated_{false};
};

using InternalEngineSharedPtr = std::shared_ptr<InternalEngine>;
using InternalEngineWeakPtr = std::weak_ptr<InternalEngine>;

} // namespace Envoy
