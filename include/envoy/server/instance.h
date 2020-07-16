#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/api/api.h"
#include "envoy/common/mutex_tracer.h"
#include "envoy/config/trace/v3/http_tracer.pb.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/context.h"
#include "envoy/http/context.h"
#include "envoy/init/manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/listen_socket.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/admin.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/hot_restart.h"
#include "envoy/server/lifecycle_notifier.h"
#include "envoy/server/listener_manager.h"
#include "envoy/server/options.h"
#include "envoy/server/overload_manager.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Server {

/**
 * An instance of the running server.
 */
class Instance {
public:
  virtual ~Instance() = default;

  /**
   * @return Admin& the global HTTP admin endpoint for the server.
   */
  virtual Admin& admin() PURE;

  /**
   * @return Api::Api& the API used by the server.
   */
  virtual Api::Api& api() PURE;

  /**
   * @return Upstream::ClusterManager& singleton for use by the entire server.
   */
  virtual Upstream::ClusterManager& clusterManager() PURE;

  /**
   * @return Ssl::ContextManager& singleton for use by the entire server.
   */
  virtual Ssl::ContextManager& sslContextManager() PURE;

  /**
   * @return Event::Dispatcher& the main thread's dispatcher. This dispatcher should be used
   *         for all singleton processing.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * @return Network::DnsResolverSharedPtr the singleton DNS resolver for the server.
   */
  virtual Network::DnsResolverSharedPtr dnsResolver() PURE;

  /**
   * Close the server's listening sockets and begin draining the listeners.
   */
  virtual void drainListeners() PURE;

  /**
   * @return DrainManager& singleton for use by the entire server.
   */
  virtual DrainManager& drainManager() PURE;

  /**
   * @return AccessLogManager for use by the entire server.
   */
  virtual AccessLog::AccessLogManager& accessLogManager() PURE;

  /**
   * Toggle whether the server fails or passes external healthchecks.
   */
  virtual void failHealthcheck(bool fail) PURE;

  /**
   * @return whether external healthchecks are currently failed or not.
   */
  virtual bool healthCheckFailed() PURE;

  /**
   * @return the server's hot restarter.
   */
  virtual HotRestart& hotRestart() PURE;

  /**
   * @return the server's init manager. This can be used for extensions that need to initialize
   *         after cluster manager init but before the server starts listening. All extensions
   *         should register themselves during configuration load. initialize() will be called on
   *         each registered target after cluster manager init but before the server starts
   *         listening. Once all targets have initialized and invoked their callbacks, the server
   *         will start listening.
   */
  virtual Init::Manager& initManager() PURE;

  /**
   * @return the server's listener manager.
   */
  virtual ListenerManager& listenerManager() PURE;

  /**
   * @return the server's global mutex tracer, if it was instantiated. Nullptr otherwise.
   */
  virtual Envoy::MutexTracer* mutexTracer() PURE;

  /**
   * @return the server's overload manager.
   */
  virtual OverloadManager& overloadManager() PURE;

  /**
   * @return the server's secret manager
   */
  virtual Secret::SecretManager& secretManager() PURE;

  /**
   * @return the server's CLI options.
   */
  virtual const Options& options() PURE;

  /**
   * @return RandomGenerator& the random generator for the server.
   */
  virtual Runtime::RandomGenerator& random() PURE;

  /**
   * @return Runtime::Loader& the singleton runtime loader for the server.
   */
  virtual Runtime::Loader& runtime() PURE;

  /**
   * @return ServerLifecycleNotifier& the singleton lifecycle notifier for the server.
   */
  virtual ServerLifecycleNotifier& lifecycleNotifier() PURE;

  /**
   * Shutdown the server gracefully.
   */
  virtual void shutdown() PURE;

  /**
   * @return whether the shutdown method has been called.
   */
  virtual bool isShutdown() PURE;

  /**
   * Shutdown the server's admin processing. This includes the admin API, stat flushing, etc.
   */
  virtual void shutdownAdmin() PURE;

  /**
   * @return Singleton::Manager& the server-wide singleton manager.
   */
  virtual Singleton::Manager& singletonManager() PURE;

  /**
   * @return the time that the server started during the current hot restart epoch.
   */
  virtual time_t startTimeCurrentEpoch() PURE;

  /**
   * @return the time that the server started the first hot restart epoch.
   */
  virtual time_t startTimeFirstEpoch() PURE;

  /**
   * @return the server-wide stats store.
   */
  virtual Stats::Store& stats() PURE;

  /**
   * @return the server-wide grpc context.
   */
  virtual Grpc::Context& grpcContext() PURE;

  /**
   * @return the server-wide http context.
   */
  virtual Http::Context& httpContext() PURE;

  /**
   * @return the server-wide process context.
   */
  virtual ProcessContextOptRef processContext() PURE;

  /**
   * @return ThreadLocal::Instance& the thread local storage engine for the server. This is used to
   *         allow runtime lockless updates to configuration, etc. across multiple threads.
   */
  virtual ThreadLocal::Instance& threadLocal() PURE;

  /**
   * @return information about the local environment the server is running in.
   */
  virtual const LocalInfo::LocalInfo& localInfo() const PURE;

  /**
   * @return the time source used for the server.
   */
  virtual TimeSource& timeSource() PURE;

  /**
   * @return the flush interval of stats sinks.
   */
  virtual std::chrono::milliseconds statsFlushInterval() const PURE;

  /**
   * Flush the stats sinks outside of a flushing interval.
   * Note: stats flushing may not be synchronous.
   * Therefore, this function may return prior to flushing taking place.
   */
  virtual void flushStats() PURE;

  /**
   * @return ProtobufMessage::ValidationContext& validation context for configuration
   *         messages.
   */
  virtual ProtobufMessage::ValidationContext& messageValidationContext() PURE;

  /**
   * @return Configuration::ServerFactoryContext& factory context for filters.
   */
  virtual Configuration::ServerFactoryContext& serverFactoryContext() PURE;

  /**
   * @return Configuration::TransportSocketFactoryContext& factory context for transport sockets.
   */
  virtual Configuration::TransportSocketFactoryContext& transportSocketFactoryContext() PURE;

  /**
   * Set the default server-wide tracer provider configuration that will be used as a fallback
   * if an "envoy.filters.network.http_connection_manager" filter that has tracing enabled doesn't
   * define a tracer provider in-place.
   *
   * Once deprecation window for the tracer provider configuration in the bootstrap config is over,
   * this method will no longer be necessary.
   */
  virtual void
  setDefaultTracingConfig(const envoy::config::trace::v3::Tracing& tracing_config) PURE;
};

} // namespace Server
} // namespace Envoy
