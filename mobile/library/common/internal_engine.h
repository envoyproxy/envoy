#pragma once

#include "envoy/server/lifecycle_notifier.h"

#include "source/common/common/logger.h"
#include "source/common/common/macros.h"
#include "source/common/common/posix/thread_impl.h"
#include "source/common/common/thread.h"

#include "absl/synchronization/notification.h"
#include "absl/types/optional.h"
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
   * @param thread_priority, an optional thread priority, between -20 and 19.
   */
  InternalEngine(std::unique_ptr<EngineCallbacks> callbacks, std::unique_ptr<EnvoyLogger> logger,
                 std::unique_ptr<EnvoyEventTracker> event_tracker,
                 absl::optional<int> thread_priority = absl::nullopt);

  /**
   * InternalEngine destructor.
   */
  ~InternalEngine();

  /**
   * Run the engine with the provided options.
   * @param options, the Envoy options, including the Bootstrap configuration and log level.
   */
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
  Event::ProvisionalDispatcher& dispatcher() const;

  /**
   * Accessor for the thread factory.
   */
  Thread::PosixThreadFactory& threadFactory() const;

  envoy_stream_t initStream();

  // These functions are wrappers around http client functions, which hand off
  // to http client functions of the same name after doing a dispatcher post
  // (thread context switch)
  envoy_status_t startStream(envoy_stream_t stream, EnvoyStreamCallbacks&& stream_callbacks,
                             bool explicit_flow_control);

  /**
   * Send the headers over an open HTTP stream. This function can be invoked
   * once and needs to be called before `sendData`.
   *
   * @param stream the stream to send headers over.
   * @param headers the headers to send.
   * @param end_stream indicates whether to close the stream locally after sending this frame.
   * @param idempotent indicates that the request is idempotent. When idempotent is set to true
   *                   Envoy Mobile will retry on HTTP/3 post-handshake failures. By default, it is
   *                   set to false.
   */
  envoy_status_t sendHeaders(envoy_stream_t stream, Http::RequestHeaderMapPtr headers,
                             bool end_stream, bool idempotent = false);

  envoy_status_t readData(envoy_stream_t stream, size_t bytes_to_read);

  /**
   * Send data over an open HTTP stream. This method can be invoked multiple times.
   *
   * @param stream the stream to send data over.
   * @param buffer the data to send.
   * @param end_stream indicates whether to close the stream locally after sending this frame.
   */
  envoy_status_t sendData(envoy_stream_t stream, Buffer::InstancePtr buffer, bool end_stream);

  /**
   * Send trailers over an open HTTP stream. This method can only be invoked once per stream.
   * Note that this method implicitly closes the stream locally.
   *
   * @param stream the stream to send trailers over.
   * @param trailers the trailers to send.
   */
  envoy_status_t sendTrailers(envoy_stream_t stream, Http::RequestTrailerMapPtr trailers);

  envoy_status_t cancelStream(envoy_stream_t stream);

  // These functions are wrappers around networkConnectivityManager functions, which hand off
  // to networkConnectivityManager after doing a dispatcher post (thread context switch)
  envoy_status_t setProxySettings(const char* host, const uint16_t port);
  envoy_status_t resetConnectivityState();

  /**
   * This function is called when the default network is available. This function is currently
   * no-op.
   */
  void onDefaultNetworkAvailable();

  /**
   * This function does the following when the default network was changed.
   *
   * - Sets the preferred network.
   * - Check for IPv6 connectivity. If there is no IPv6 no connectivity, it will call
   *   `setIpVersionToRemove` in the DNS cache implementation to remove the IPv6 addresses from
   *   the DNS response in the subsequent DNS resolutions.
   * - Force refresh the hosts in the DNS cache (will take `setIpVersionToRemove` into account).
   * - Optionally (if configured) clear HTTP/3 broken status.
   */
  void onDefaultNetworkChanged(NetworkType network);

  /**
   * This functions does the following when the default network is unavailable.
   *
   * - Cancel the DNS pending queries.
   * - Stop the DNS timeout and refresh timers.
   */
  void onDefaultNetworkUnavailable();

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
  // Needs access to the private constructor.
  GTEST_FRIEND_CLASS(InternalEngineTest, ThreadCreationFailed);

  InternalEngine(std::unique_ptr<EngineCallbacks> callbacks, std::unique_ptr<EnvoyLogger> logger,
                 std::unique_ptr<EnvoyEventTracker> event_tracker,
                 absl::optional<int> thread_priority, Thread::PosixThreadFactoryPtr thread_factory);

  envoy_status_t main(std::shared_ptr<Envoy::OptionsImplBase> options);
  static void logInterfaces(absl::string_view event,
                            std::vector<Network::InterfacePair>& interfaces);
  /** Returns true if there is IPv6 connectivity. */
  static bool hasIpV6Connectivity();

  Thread::PosixThreadFactoryPtr thread_factory_;
  Event::Dispatcher* event_dispatcher_{};
  Stats::ScopeSharedPtr client_scope_;
  Stats::StatNameSetPtr stat_name_set_;
  std::unique_ptr<EngineCallbacks> callbacks_;
  std::unique_ptr<EnvoyLogger> logger_;
  std::unique_ptr<EnvoyEventTracker> event_tracker_;
  absl::optional<int> thread_priority_;
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
  absl::Notification engine_running_;
};

} // namespace Envoy
